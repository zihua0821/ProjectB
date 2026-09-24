// SmartObjectManagerComponent.cpp
#include "Mass/SmartObjectManagerComponent.h"

#include "SmartObjectDefinition.h"
#include "SmartObjectSubsystem.h"
#include "Crops/CropData.h"             // EEditMode（操作模式 → Slot 映射）
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

	// 认领账本：两个 Slot（0=Plot / 1=Crop）
	ClaimLedger.SetNum(2);

	// 收集全部坐标进入待注册队列
	PendingCoords = CellManager->GetAllCoords();
	PendingIndex = 0;
	bRegistrationDone = false;
	RegistrationSuccessCount = 0;

	// 订阅 CellManager 的 Slot 状态意图委托（耕地/播种/移除 → 启停）
	CellManager->OnCellSlotStateChanged.AddUObject(this, &ThisClass::HandleCellSlotStateChanged);

	UE_LOG(LogTemp, Log, TEXT("[SmartObjectManager] Batch registration start: %d cells"), PendingCoords.Num());
}

void USmartObjectManagerComponent::HandleCellSlotStateChanged(const FIntPoint Coords, const int32 SlotIndex, const bool bEnabled)
{
	// 注册完成前不响应（句柄映射尚未就绪）
	if (!bRegistrationDone)
	{
		return;
	}

	if (SlotIndex == 0)
	{
		SetPlotSlotEnabled(Coords, bEnabled);
	}
	else if (SlotIndex == 1)
	{
		SetCropSlotEnabled(Coords, bEnabled);
	}
}

void USmartObjectManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	// TickComponent 驱动分批注册；若未 Initialize（如纯蓝图挂载未接线）则保持等待
}

void USmartObjectManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bRegistrationDone || !SmartObjectSubsystem || PendingCoords.Num() == 0)
	{
		return;
	}

	// 分批注册：每帧处理 BatchSize 个
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
		// 运行时停 Tick 必须用 SetTickFunctionEnable（改 bCanEverTick 只在注册时生效，不注销已注册 Tick）
		SetComponentTickEnabled(false);
		UE_LOG(LogTemp, Log, TEXT("[SmartObjectManager] Registration done: %d/%d registered (%d failed)"),
			RegistrationSuccessCount, PendingCoords.Num(), PendingCoords.Num() - RegistrationSuccessCount);
	}
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

	// 捕获两个 Slot 句柄（Plot=0, Crop=1）
	FSmartObjectSlotHandle PlotSlotHandle;
	FSmartObjectSlotHandle CropSlotHandle;
	if (!CaptureSlotHandles(ObjectHandle, WorldTransform, PlotSlotHandle, CropSlotHandle))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SmartObjectManager] CaptureSlotHandles failed at (%d, %d)"), Coords.X, Coords.Y);
		return false;
	}

	// 写入映射
	FCellSmartObjectEntry Entry;
	Entry.ObjectHandle = ObjectHandle;
	Entry.PlotSlotHandle = PlotSlotHandle;
	Entry.CropSlotHandle = CropSlotHandle;
	CoordToEntry.Add(Coords, Entry);

	// 默认禁用（状态驱动启停：耕地/播种时才 Enable）
	SmartObjectSubsystem->SetSlotEnabled(PlotSlotHandle, false);
	SmartObjectSubsystem->SetSlotEnabled(CropSlotHandle, false);

	return true;
}

bool USmartObjectManagerComponent::CaptureSlotHandles(const FSmartObjectHandle ObjectHandle, const FTransform& WorldTransform, FSmartObjectSlotHandle& OutPlotSlot, FSmartObjectSlotHandle& OutCropSlot)
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

	// 在结果中找到属于本实例的两个 Slot
	bool bFoundPlot = false;
	bool bFoundCrop = false;
	for (const FSmartObjectRequestResult& Result : Results)
	{
		if (Result.SmartObjectHandle != ObjectHandle)
		{
			continue;
		}

		if (Result.SlotHandle.GetSlotIndex() == 0)
		{
			OutPlotSlot = Result.SlotHandle;
			bFoundPlot = true;
		}
		else if (Result.SlotHandle.GetSlotIndex() == 1)
		{
			OutCropSlot = Result.SlotHandle;
			bFoundCrop = true;
		}
	}

	return bFoundPlot && bFoundCrop;
}

bool USmartObjectManagerComponent::GetObjectHandle(const FIntPoint Coords, FSmartObjectHandle& OutHandle) const
{
	const FCellSmartObjectEntry* Entry = CoordToEntry.Find(Coords);
	if (!Entry)
	{
		return false;
	}
	OutHandle = Entry->ObjectHandle;
	return true;
}

bool USmartObjectManagerComponent::GetSlotHandle(const FIntPoint Coords, const int32 SlotIndex, FSmartObjectSlotHandle& OutHandle) const
{
	const FCellSmartObjectEntry* Entry = CoordToEntry.Find(Coords);
	if (!Entry)
	{
		return false;
	}

	if (SlotIndex == 0)
	{
		OutHandle = Entry->PlotSlotHandle;
	}
	else if (SlotIndex == 1)
	{
		OutHandle = Entry->CropSlotHandle;
	}
	else
	{
		return false;
	}
	return OutHandle.IsValid();
}

bool USmartObjectManagerComponent::GetPlotSlotHandle(const FIntPoint Coords, FSmartObjectSlotHandle& OutHandle) const
{
	return GetSlotHandle(Coords, 0, OutHandle);
}

bool USmartObjectManagerComponent::GetCropSlotHandle(const FIntPoint Coords, FSmartObjectSlotHandle& OutHandle) const
{
	return GetSlotHandle(Coords, 1, OutHandle);
}

bool USmartObjectManagerComponent::SetPlotSlotEnabled(const FIntPoint Coords, const bool bEnabled) const
{
	FSmartObjectSlotHandle SlotHandle;
	if (!GetPlotSlotHandle(Coords, SlotHandle) || !SmartObjectSubsystem)
	{
		return false;
	}
	return SmartObjectSubsystem->SetSlotEnabled(SlotHandle, bEnabled);
}

bool USmartObjectManagerComponent::SetCropSlotEnabled(const FIntPoint Coords, const bool bEnabled) const
{
	FSmartObjectSlotHandle SlotHandle;
	if (!GetCropSlotHandle(Coords, SlotHandle) || !SmartObjectSubsystem)
	{
		return false;
	}
	return SmartObjectSubsystem->SetSlotEnabled(SlotHandle, bEnabled);
}

FSmartObjectClaimHandle USmartObjectManagerComponent::TryClaimSlot(const FIntPoint Coords, const int32 SlotIndex, const ESmartObjectClaimPriority Priority) const
{
	FSmartObjectSlotHandle SlotHandle;
	if (!GetSlotHandle(Coords, SlotIndex, SlotHandle) || !SmartObjectSubsystem)
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}

	// 认领失败（未启用/已被同级或更高优先级占用）时引擎返回无效句柄
	const FSmartObjectClaimHandle ClaimHandle = SmartObjectSubsystem->MarkSlotAsClaimed(SlotHandle, Priority, FConstStructView());

	// 账本：成功则记录（抢占时新句柄覆盖旧值 → 旧持有者检测失权；账本未初始化/越界时跳过）
	if (ClaimHandle.IsValid() && ClaimLedger.IsValidIndex(SlotIndex))
	{
		ClaimLedger[SlotIndex].Add(Coords, ClaimHandle);
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
	for (int32 SlotIndex = 0; SlotIndex < ClaimLedger.Num(); ++SlotIndex)
	{
		for (auto It = ClaimLedger[SlotIndex].CreateIterator(); It; ++It)
		{
			if (It.Value() == ClaimHandle)
			{
				It.RemoveCurrent();
			}
		}
	}
	return SmartObjectSubsystem->MarkSlotAsFree(ClaimHandle);
}

bool USmartObjectManagerComponent::IsClaimValid(const FIntPoint Coords, const int32 SlotIndex, const FSmartObjectClaimHandle& ClaimHandle) const
{
	// 无效句柄或账本未就绪/越界：视为无需认领（调用方自行处理）
	if (!ClaimHandle.IsValid() || !ClaimLedger.IsValidIndex(SlotIndex))
	{
		return false;
	}
	const FSmartObjectClaimHandle* Current = ClaimLedger[SlotIndex].Find(Coords);
	return Current != nullptr && (*Current == ClaimHandle);
}

bool USmartObjectManagerComponent::IsSlotEnabled(const FIntPoint Coords, const int32 SlotIndex) const
{
	FSmartObjectSlotHandle SlotHandle;
	if (!GetSlotHandle(Coords, SlotIndex, SlotHandle) || !SmartObjectSubsystem)
	{
		return false;
	}
	const FSmartObjectSlotView SlotView = SmartObjectSubsystem->GetSlotView(SlotHandle);
	return SlotView.IsEnabled();
}

int32 USmartObjectManagerComponent::GetSlotIndexForEditMode(const EEditMode EditMode)
{
	switch (EditMode)
	{
	// 作物类操作（Crop Slot = 1）
	case EEditMode::PlantCrop:
	case EEditMode::HarvestCrop:
	case EEditMode::RemoveCrop:
	case EEditMode::CropDried:
	case EEditMode::UnCropDried:
		return 1;

	// 地块类操作（Plot Slot = 0）
	case EEditMode::Reset:
	case EEditMode::Plow:
	case EEditMode::UnPlow:
	case EEditMode::Wet:
	case EEditMode::UnWet:
	case EEditMode::Fertilize:
	case EEditMode::UnFertilize:
	case EEditMode::Weed:
	case EEditMode::UnWeed:
	case EEditMode::Pest:
	case EEditMode::UnPest:
		return 0;

	default:
		// None / Count：无操作或无效
		return -1;
	}
}
