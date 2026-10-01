// SmartObjectManagerComponent.cpp
#include "Mass/SmartObjectManagerComponent.h"

#include "SmartObjectDefinition.h"
#include "DrawDebugHelpers.h"                        // 槽位球调试可视化
#include "Mass/SatelliteManagerComponent.h"        // FRuntimeOrbitData（轨道数据定义）
#include "Planet/PlanetBlueprintFunctionLibrary.h" // ComputeOrbitWorldPosition（槽位位置）
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

	// 注：注册完成后槽位推进不在本组件 Tick 内做——由 SatelliteOrbitMovementProcessor 在 Mass 帧序内驱动
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

// ---- 轨道槽位（回轨槽位：与格子同构的智能对象实例） ----

void USmartObjectManagerComponent::RegisterOrbitSlots(const TArray<FRuntimeOrbitData>& Orbits, const int32 SlotMultiplier)
{
	if (!SmartObjectSubsystem || !OrbitSlotDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SmartObjectManager] Orbit slots skipped: subsystem or OrbitSlotDefinition missing"));
		return;
	}

	// 全量注册 = 逐轨单轨注册（Initialize 路径；单轨版自带"先销毁该轨旧实例"——不影响其他轨道）
	for (const FRuntimeOrbitData& Orbit : Orbits)
	{
		RegisterSingleOrbitSlots(Orbit, SlotMultiplier);
	}

	UE_LOG(LogTemp, Log, TEXT("[SmartObjectManager] Orbit slots registered: %d orbit(s)"), OrbitSlots.Num());
}

void USmartObjectManagerComponent::RegisterSingleOrbitSlots(const FRuntimeOrbitData& Orbit, const int32 SlotMultiplier)
{
	if (!SmartObjectSubsystem || !OrbitSlotDefinition)
	{
		return;
	}

	// 先清该轨旧槽（销毁实例 + 移条目；其他轨道的槽与句柄不受影响）
	UnregisterOrbitSlots(Orbit.OrbitName);

	const int32 SafeMultiplier = FMath::Max(1, SlotMultiplier);
	const int32 SlotCount = FMath::Max(0, Orbit.SatelliteCount) * SafeMultiplier;
	if (SlotCount == 0)
	{
		return;
	}

	FOrbitSlotEntry Entry;
	// 轨道几何（槽位位置随相位推进：槽 i = 基准相位 + 360×i/N）
	Entry.OrbitTransform = Orbit.Transform;
	Entry.OrbitRadius = Orbit.Radius;
	Entry.AngularVelocity = Orbit.AngularVelocity;
	Entry.bClockwise = Orbit.bClockwise;
	Entry.CurrentPhaseDeg = 0.f;   // 槽 0 起始相位（星 i 认领槽 i×K → 相位恰好均分）
	Entry.ObjectHandles.Reserve(SlotCount);
	Entry.SlotWorldPositions.Reserve(SlotCount);
	Entry.SlotHandles.Reserve(SlotCount);

	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		// 槽位 = 均分相位的固定点（与卫星初始相位分配一致）
		const float SlotPhaseDeg = 360.0f * static_cast<float>(SlotIndex) / static_cast<float>(SlotCount);
		const FVector SlotWorldPosition = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Orbit.Transform, Orbit.Radius, SlotPhaseDeg);
		const FTransform SlotTransform(FRotator::ZeroRotator, SlotWorldPosition);

		const FSmartObjectHandle ObjectHandle = SmartObjectSubsystem->CreateSmartObject(
			*OrbitSlotDefinition, SlotTransform, FConstStructView());
		if (!ObjectHandle.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[SmartObjectManager] Orbit slot create failed: %s[%d]"), *Orbit.OrbitName.ToString(), SlotIndex);
			continue;
		}

		// 单 Slot 定义：微查询捕获 Slot 0
		FSmartObjectRequest Request;
		Request.QueryBox = FBox::BuildAABB(SlotWorldPosition, FVector(1.0f));
		TArray<FSmartObjectRequestResult> Results;
		FSmartObjectSlotHandle CapturedSlot;
		if (SmartObjectSubsystem->FindSmartObjects(Request, Results))
		{
			for (const FSmartObjectRequestResult& Result : Results)
			{
				if (Result.SmartObjectHandle == ObjectHandle && Result.SlotHandle.GetSlotIndex() == 0)
				{
					CapturedSlot = Result.SlotHandle;
					break;
				}
			}
		}
		if (!CapturedSlot.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[SmartObjectManager] Orbit slot capture failed: %s[%d]"), *Orbit.OrbitName.ToString(), SlotIndex);
			continue;
		}

		Entry.ObjectHandles.Add(ObjectHandle);
		Entry.SlotWorldPositions.Add(SlotWorldPosition);
		Entry.SlotHandles.Add(CapturedSlot);
	}

	OrbitSlots.Add(Orbit.OrbitName, MoveTemp(Entry));
}

void USmartObjectManagerComponent::UnregisterOrbitSlots(const FName OrbitName)
{
	if (!SmartObjectSubsystem)
	{
		return;
	}
	if (const FOrbitSlotEntry* OldEntry = OrbitSlots.Find(OrbitName))
	{
		// 资源生命周期：实例不随条目移除消失——必须显式销毁（旧认领由卫星销毁链路先行归还）
		for (const FSmartObjectHandle& ObjectHandle : OldEntry->ObjectHandles)
		{
			if (ObjectHandle.IsValid())
			{
				SmartObjectSubsystem->DestroySmartObject(ObjectHandle);
			}
		}
		OrbitSlots.Remove(OrbitName);
	}
}

void USmartObjectManagerComponent::UpdateOrbitSlots(const float DeltaSeconds)
{
	if (!SmartObjectSubsystem || OrbitSlots.Num() == 0)
	{
		return;
	}

	for (TPair<FName, FOrbitSlotEntry>& Pair : OrbitSlots)
	{
		FOrbitSlotEntry& Entry = Pair.Value;
		const int32 SlotCount = Entry.SlotHandles.Num();
		if (SlotCount == 0)
		{
			continue;
		}

		// 相位推进（与卫星同速：槽位随轨道旋转）
		Entry.CurrentPhaseDeg += (Entry.bClockwise ? -1.0f : 1.0f) * Entry.AngularVelocity * DeltaSeconds;
		Entry.CurrentPhaseDeg = FMath::Fmod(Entry.CurrentPhaseDeg, 360.0f);
		if (Entry.CurrentPhaseDeg < 0.0f)
		{
			Entry.CurrentPhaseDeg += 360.0f;
		}

		// 同步槽位世界位置（引擎智能对象实例位置不开放运行时修改，
		// 槽位坐标由本组件内部缓存提供：近点判定 / 回轨终点 / 未来调试均取此缓存；
		// 认领机制仅依赖句柄，不受影响）
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			const float SlotPhaseDeg = Entry.CurrentPhaseDeg + 360.0f * static_cast<float>(SlotIndex) / static_cast<float>(SlotCount);
			const FVector SlotWorldPosition = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Entry.OrbitTransform, Entry.OrbitRadius, SlotPhaseDeg);
			if (Entry.SlotWorldPositions.IsValidIndex(SlotIndex))
			{
				Entry.SlotWorldPositions[SlotIndex] = SlotWorldPosition;
			}

			// ---- 调试可视化：槽位球（每帧刷新，单帧存活；与卫星同尺寸感）----
			if (UWorld* World = GetWorld())
			{
				DrawDebugSphere(World, SlotWorldPosition, 60.f, 8, FColor::Cyan,
					/*bPersistent=*/false, /*LifeTime=*/-1.f, /*DepthPriority=*/0, /*Thickness=*/0.f);
			}
		}
	}
}

int32 USmartObjectManagerComponent::GetOrbitSlotCount(const FName OrbitName) const
{
	const FOrbitSlotEntry* Entry = OrbitSlots.Find(OrbitName);
	return Entry ? Entry->SlotHandles.Num() : 0;
}

bool USmartObjectManagerComponent::GetOrbitSlotWorldPosition(const FName OrbitName, const int32 SlotIndex, FVector& OutWorldPosition) const
{
	const FOrbitSlotEntry* Entry = OrbitSlots.Find(OrbitName);
	if (!Entry || !Entry->SlotWorldPositions.IsValidIndex(SlotIndex))
	{
		return false;
	}
	OutWorldPosition = Entry->SlotWorldPositions[SlotIndex];
	return true;
}

bool USmartObjectManagerComponent::GetOrbitSlotPhase(const FName OrbitName, const int32 SlotIndex, float& OutPhaseDeg) const
{
	const FOrbitSlotEntry* Entry = OrbitSlots.Find(OrbitName);
	if (!Entry || Entry->SlotHandles.Num() == 0 || SlotIndex < 0)
	{
		return false;
	}
	const int32 SlotCount = Entry->SlotHandles.Num();
	if (SlotIndex >= SlotCount)
	{
		return false;
	}
	OutPhaseDeg = Entry->CurrentPhaseDeg + 360.0f * static_cast<float>(SlotIndex) / static_cast<float>(SlotCount);
	return true;
}

bool USmartObjectManagerComponent::GetOrbitSlotWorldPositionByClaim(const FName OrbitName, const FSmartObjectClaimHandle& ClaimHandle, FVector& OutWorldPosition) const
{
	if (!ClaimHandle.IsValid())
	{
		return false;
	}
	const int32 SlotIndex = ResolveSlotIndexByClaim(OrbitName, ClaimHandle);
	return GetOrbitSlotWorldPosition(OrbitName, SlotIndex, OutWorldPosition);
}

bool USmartObjectManagerComponent::GetOrbitSlotPhaseByClaim(const FName OrbitName, const FSmartObjectClaimHandle& ClaimHandle, float& OutPhaseDeg) const
{
	if (!ClaimHandle.IsValid())
	{
		return false;
	}
	const int32 SlotIndex = ResolveSlotIndexByClaim(OrbitName, ClaimHandle);
	return GetOrbitSlotPhase(OrbitName, SlotIndex, OutPhaseDeg);
}

int32 USmartObjectManagerComponent::ResolveSlotIndexByClaim(const FName OrbitName, const FSmartObjectClaimHandle& ClaimHandle) const
{
	const FOrbitSlotEntry* Entry = OrbitSlots.Find(OrbitName);
	if (!Entry || !ClaimHandle.SlotHandle.IsValid())
	{
		return -1;
	}
	// 反查：槽位句柄 → 轨道槽索引（每轨槽数 = 卫星数，量级极小，直接遍历）
	return Entry->SlotHandles.IndexOfByKey(ClaimHandle.SlotHandle);
}

FSmartObjectClaimHandle USmartObjectManagerComponent::TryClaimOrbitSlot(const FName OrbitName, const int32 SlotIndex, const ESmartObjectClaimPriority Priority) const
{
	const FOrbitSlotEntry* Entry = OrbitSlots.Find(OrbitName);
	if (!Entry || !Entry->SlotHandles.IsValidIndex(SlotIndex) || !SmartObjectSubsystem)
	{
		return FSmartObjectClaimHandle::InvalidHandle;
	}
	// 认领失败（已被占）时引擎返回无效句柄（槽位占用情况通过句柄有效性判断）
	return SmartObjectSubsystem->MarkSlotAsClaimed(Entry->SlotHandles[SlotIndex], Priority, FConstStructView());
}

bool USmartObjectManagerComponent::IsSlotClaimed(const FIntPoint Coords, const int32 SlotIndex) const
{
	// 账本 = 当前认领者（所有认领入口统一走本组件）——识别"幽灵认领"（持有者不会来服务）
	return ClaimLedger.IsValidIndex(SlotIndex) && ClaimLedger[SlotIndex].Contains(Coords);
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
