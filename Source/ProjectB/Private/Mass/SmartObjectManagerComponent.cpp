// SmartObjectManagerComponent.cpp
#include "Mass/SmartObjectManagerComponent.h"

#include "SmartObjectDefinition.h"
#include "SmartObjectSubsystem.h"
#include "Farm/CellManagerComponent.h"
#include "Mass/FarmSmartObjectTypes.h"   // FCoordinatePayload

USmartObjectManagerComponent::USmartObjectManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USmartObjectManagerComponent::Initialize(UCellManagerComponent* InCellManager)
{
	CellManager = InCellManager;
	if (!CellManager || !SmartObjectDefinition)
	{
		UE_LOG(LogTemp, Error, TEXT("[SmartObjectManager] Initialize failed: CellManager=%s Definition=%s"),
			CellManager ? TEXT("ok") : TEXT("null"), SmartObjectDefinition ? *SmartObjectDefinition->GetName() : TEXT("null"));
		return;
	}

	SmartObjectSubsystem = GetWorld()->GetSubsystem<USmartObjectSubsystem>();
	if (!SmartObjectSubsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[SmartObjectManager] USmartObjectSubsystem not found"));
		return;
	}

	// 收集全部坐标进入待注册队列
	PendingCoords = CellManager->GetAllCoords();
	PendingIndex = 0;
	bRegistrationDone = false;
	RegistrationSuccessCount = 0;

	UE_LOG(LogTemp, Log, TEXT("[SmartObjectManager] Batch registration start: %d cells"), PendingCoords.Num());
}

void USmartObjectManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!SmartObjectSubsystem)
	{
		return;
	}

	// 注册阶段：分批处理（每帧 BatchSize 个）
	if (!bRegistrationDone && PendingCoords.Num() > 0)
	{
		const int32 EndIndex = FMath::Min(PendingIndex + BatchSize, PendingCoords.Num());
		for (; PendingIndex < EndIndex; ++PendingIndex)
		{
			if (RegisterCell(PendingCoords[PendingIndex]))
			{
				++RegistrationSuccessCount;
			}
		}

		if (PendingIndex >= PendingCoords.Num())
		{
			bRegistrationDone = true;
			UE_LOG(LogTemp, Log, TEXT("[SmartObjectManager] Registration done: %d/%d registered (%d failed)"),
				RegistrationSuccessCount, PendingCoords.Num(), PendingCoords.Num() - RegistrationSuccessCount);
		}
		return;
	}

	// 注：注册完成后槽位推进不在本组件 Tick 内做——由 USatelliteSlotSyncProcessor（专职 GT）在 Mass 帧序内驱动
	//（先槽后星、同帧同序：渲染同步处理器随后读到同帧槽缓存，消除恒定错位）
}

bool USmartObjectManagerComponent::RegisterCell(const FIntPoint Coords)
{
	// 格子世界坐标（读 FCellStaticFragment.Transform）
	FTransform WorldTransform;
	if (!CellManager->GetCellWorldTransform(Coords, WorldTransform))
	{
		return false;
	}

	// 创建智能对象实例（携带坐标荷载）
	const FCoordinatePayload Payload(Coords);
	const FSmartObjectHandle ObjectHandle = SmartObjectSubsystem->CreateSmartObject(
		*SmartObjectDefinition,
		WorldTransform,
		FConstStructView::Make(Payload)
	);
	if (!ObjectHandle.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SmartObjectManager] CreateSmartObject failed at (%d, %d)"), Coords.X, Coords.Y);
		return false;
	}

	// 捕获 Slot 句柄（单 Slot——Slot 0）
	FSmartObjectSlotHandle SlotHandle;
	if (!CaptureSlotHandle(ObjectHandle, WorldTransform, SlotHandle))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SmartObjectManager] CaptureSlotHandle failed at (%d, %d)"), Coords.X, Coords.Y);
		return false;
	}

	// 写入映射（Slot 保持注册启用态——恒可用；服不服务由任务侧状态判断）
	FCellSmartObjectEntry Entry;
	Entry.ObjectHandle = ObjectHandle;
	Entry.SlotHandle = SlotHandle;
	CoordToEntry.Add(Coords, Entry);

	return true;
}

bool USmartObjectManagerComponent::CaptureSlotHandle(const FSmartObjectHandle ObjectHandle, const FTransform& WorldTransform, FSmartObjectSlotHandle& OutSlot)
{
	if (!SmartObjectSubsystem)
	{
		return false;
	}

	// 以实例位置为中心的微查询（Filter 全空 → 空间命中即返回全部 Slot）
	FSmartObjectRequest Request;
	Request.QueryBox = FBox::BuildAABB(WorldTransform.GetLocation(), FVector(1.0f));

	TArray<FSmartObjectRequestResult> Results;
	if (!SmartObjectSubsystem->FindSmartObjects(Request, Results))
	{
		return false;
	}

	// 在结果中找到属于本实例的 Slot（单 Slot：Slot 0）
	for (const FSmartObjectRequestResult& Result : Results)
	{
		if (Result.SmartObjectHandle != ObjectHandle)
		{
			continue;
		}
		if (Result.SlotHandle.GetSlotIndex() == 0)
		{
			OutSlot = Result.SlotHandle;
			return true;
		}
	}
	return false;
}

bool USmartObjectManagerComponent::GetCellSlotHandle(const FIntPoint Coords, FSmartObjectSlotHandle& OutHandle) const
{
	const FCellSmartObjectEntry* Entry = CoordToEntry.Find(Coords);
	if (!Entry)
	{
		return false;
	}
	OutHandle = Entry->SlotHandle;
	return OutHandle.IsValid();
}

FSmartObjectClaimHandle USmartObjectManagerComponent::TryClaimSlot(const FIntPoint Coords, const ESmartObjectClaimPriority Priority) const
{
	FSmartObjectSlotHandle SlotHandle;
	if (!GetCellSlotHandle(Coords, SlotHandle) || !SmartObjectSubsystem)
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	// 认领失败（已被同级或更高优先级占用）时引擎返回无效句柄
	const FSmartObjectClaimHandle ClaimHandle = SmartObjectSubsystem->MarkSlotAsClaimed(SlotHandle, Priority, FConstStructView());

	// 账本：成功则记录（抢占时新句柄覆盖旧值 → 旧持有者检测失权）
	if (ClaimHandle.IsValid())
	{
		ClaimLedger.Add(Coords, ClaimHandle);
	}
	return ClaimHandle;
}

bool USmartObjectManagerComponent::ReleaseClaim(const FSmartObjectClaimHandle& ClaimHandle) const
{
	if (!SmartObjectSubsystem || !ClaimHandle.IsValid())
	{
		return false;
	}

	// 账本清理：仅清除与句柄相等的项（避免误清已被新认领者覆盖的值）
	for (auto It = ClaimLedger.CreateIterator(); It; ++It)
	{
		if (It.Value() == ClaimHandle)
		{
			It.RemoveCurrent();
		}
	}
	return SmartObjectSubsystem->MarkSlotAsFree(ClaimHandle);
}

bool USmartObjectManagerComponent::IsClaimValid(const FIntPoint Coords, const FSmartObjectClaimHandle& ClaimHandle) const
{
	// 无效句柄：视为失权（调用方自行处理）
	if (!ClaimHandle.IsValid())
	{
		return false;
	}
	const FSmartObjectClaimHandle* Current = ClaimLedger.Find(Coords);
	return Current != nullptr && (*Current == ClaimHandle);
}

bool USmartObjectManagerComponent::IsSlotClaimed(const FIntPoint Coords) const
{
	// 账本 = 当前认领者（所有认领入口统一走本组件）——识别"幽灵认领"（持有者不会来服务）
	return ClaimLedger.Contains(Coords);
}
