// SatelliteStateTreeTasks.cpp
#include "Mass/SatelliteStateTreeTasks.h"

#include "Farm/CellManagerComponent.h"
#include "Mass/CellMassFragments.h"              // FPlotStateFragment / FCropStateFragment（缺口判定）
#include "Mass/FarmSmartObjectTypes.h"           // UFarmActionBehaviorBase（作业时长）
#include "Mass/SatelliteManagerComponent.h"      // FSatelliteScoringWeights / USatelliteManagerComponent
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SmartObjectManagerComponent.h"
#include "MassEntityManager.h"
#include "MassStateTreeExecutionContext.h"
#include "Planet/OrbitManagerComponent.h"          // FOrbitDefinition（轨道定义权威结构）
#include "Planet/PlanetBlueprintFunctionLibrary.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "SmartObjectSubsystem.h"

namespace
{
	/** 卫星认领优先级（玩家 High 可抢占，见《智能对象架构与落地.md》7.5） */
	constexpr ESmartObjectClaimPriority SatelliteClaimPriority = ESmartObjectClaimPriority::Normal;

	/** 选点节流间隔（秒；仅巡航态生效，避免每帧遍历候选集） */
	constexpr double SelectIntervalSeconds = 0.2;

	/** 作业时长缺省值（秒；认领句柄/行为定义缺失时使用） */
	constexpr float DefaultActionDuration = 2.0f;

	/** 卫星任务数据聚合（fragment + 共享引用） */
	struct FSattelliteTaskData
	{
		FMassEntityManager* EntityManager = nullptr;
		FMassEntityHandle Entity;
		const FMassSatelliteSharedFragment* Shared = nullptr;
		FMassOrbitFragment* Orbit = nullptr;
		FMassSatelliteTaskFragment* Task = nullptr;

		bool IsValid() const { return EntityManager && Shared && Orbit && Task; }
	};

	/** 读取卫星任务数据（任一缺失时 IsValid() 为 false） */
	FSattelliteTaskData GetSatelliteTaskData(FStateTreeExecutionContext& Context)
	{
		FSattelliteTaskData Data;
		const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
		Data.EntityManager = &MassContext.GetEntityManager();
		Data.Entity = MassContext.GetEntity();
		Data.Shared = Data.EntityManager->GetConstSharedFragmentDataPtr<FMassSatelliteSharedFragment>(Data.Entity);
		Data.Orbit = Data.EntityManager->GetFragmentDataPtr<FMassOrbitFragment>(Data.Entity);
		Data.Task = Data.EntityManager->GetFragmentDataPtr<FMassSatelliteTaskFragment>(Data.Entity);
		return Data;
	}

	/** 选点候选（评分结果） */
	struct FSatelliteTargetCandidate
	{
		FIntPoint Coord = FIntPoint(-1, -1);
		EEditMode EditMode = EEditMode::None;
		float Score = 0.f;

		bool IsValid() const { return Coord != FIntPoint(-1, -1) && EditMode != EEditMode::None; }
	};

	/** 单元格缺口项（操作类型 + 分数） */
	struct FCellGap
	{
		EEditMode EditMode = EEditMode::None;
		float Score = 0.f;
	};

	/**
	 * 收集单元格全部缺口（缺什么补什么）并按分数降序排序
	 * —— 选点与队列构建【同源】：选点取首项（最高分）；队列取全列表
	 * 缺口维度：成熟收获 / 已收获重播 / 杂草 / 虫害 / 缺水 / 缺肥
	 * 注：暂不含"主动耕地"（耕地状态是活跃农田的前提）
	 * @return true=存在缺口（OutGaps 按分数降序）
	 */
	bool CollectCellGaps(const FPlotStateFragment& PlotState, const FCropStateFragment* CropState, const FSatelliteScoringWeights& Weights, TArray<FCellGap>& OutGaps)
	{
		OutGaps.Reset();

		// 作物缺口：成熟 → 收获；已收获 → 重播（PlantCrop 内部沿用格子已有作物的 CropID）
		if (CropState != nullptr)
		{
			if (CropState->CropGrowthStage == ECropGrowthStage::Mature)
			{
				OutGaps.Add({ EEditMode::HarvestCrop, Weights.HarvestWeight });
			}
			else if (CropState->CropGrowthStage == ECropGrowthStage::Harvested)
			{
				OutGaps.Add({ EEditMode::PlantCrop, Weights.ReplantWeight });
			}
		}

		// 地块缺口：杂草 / 虫害（二值权重）
		if (PlotState.StateData.HasState(EPlotState::WeedInfested))
		{
			OutGaps.Add({ EEditMode::UnWeed, Weights.WeedWeight });
		}
		if (PlotState.StateData.HasState(EPlotState::PestInfested))
		{
			OutGaps.Add({ EEditMode::UnPest, Weights.PestWeight });
		}

		// 地块缺口：缺水 / 缺肥（阈值判定 + 连续计分）
		if (PlotState.MoisturePercentage < Weights.MoistureThreshold)
		{
			OutGaps.Add({ EEditMode::Wet, (1.f - PlotState.MoisturePercentage) * Weights.MoistureWeight });
		}
		if (PlotState.FertilityPercentage < Weights.FertilityThreshold)
		{
			OutGaps.Add({ EEditMode::Fertilize, (1.f - PlotState.FertilityPercentage) * Weights.FertilityWeight });
		}

		// 分数降序（最严重的先做——选点取首项、队列按此顺序执行）
		OutGaps.Sort([](const FCellGap& A, const FCellGap& B) { return A.Score > B.Score; });
		return OutGaps.Num() > 0;
	}

	/**
	 * 构建单元格作业队列（独立函数；与选点判定同源——CollectCellGaps）
	 * 顺序：分数降序（最严重的先做）；标准 FIFO 出队（执行侧）
	 */
	void BuildCellWorkQueue(const TArray<FCellGap>& Gaps, TArray<TEnumAsByte<EEditMode>>& OutQueue)
	{
		OutQueue.Reset();
		OutQueue.Reserve(Gaps.Num());
		for (const FCellGap& Gap : Gaps)
		{
			OutQueue.Add(Gap.EditMode);
		}
	}

	/**
	 * 失去所有权时的统一清理（取消当前任务：清本地任务数据；调用方直接 return 其返回值）
	 * 位置覆盖不清（若已在悬停点，由回轨流程接管）
	 * @return 统一返回 Failed（供状态树按配置跳转规定状态）
	 */
	EStateTreeRunStatus HandleOwnershipLost(const FSattelliteTaskData& Data, const TCHAR* TaskName)
	{
		if (Data.Task != nullptr)
		{
			UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] %s: ownership lost at cell (%d, %d) — cancel task"),
				TaskName, Data.Task->TargetCoord.X, Data.Task->TargetCoord.Y);
			Data.Task->TargetCoord = FIntPoint(-1, -1);
			Data.Task->TargetEditMode = EEditMode::None;
			Data.Task->ClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
			Data.Task->WorkQueue.Reset();
			Data.Task->ClaimHandles.Reset();
		}
		return EStateTreeRunStatus::Failed;
	}

	/** 读取认领句柄对应的作业时长（行为定义；无认领/无定义时用缺省） */
	float ReadActionDuration(const UWorld* World, const FSmartObjectClaimHandle& ClaimHandle)
	{
		if (!ClaimHandle.IsValid() || World == nullptr)
		{
			return DefaultActionDuration;
		}
		// 注意：GetBehaviorDefinition<T> 为非 const 成员函数，Subsystem 指针不可加 const
		USmartObjectSubsystem* SmartObjectSubsystem = World->GetSubsystem<USmartObjectSubsystem>();
		if (SmartObjectSubsystem != nullptr)
		{
			if (const UFarmActionBehaviorBase* Behavior = SmartObjectSubsystem->GetBehaviorDefinition<UFarmActionBehaviorBase>(ClaimHandle))
			{
				return Behavior->Duration;
			}
		}
		return DefaultActionDuration;
	}

	/** 开始队列首项（重置计时与时长；需 WorkQueue 非空） */
	void BeginQueueItem(const FMassSatelliteTaskFragment& Task, FSatelliteExecuteActionTaskInstanceData& InstanceData, const UWorld* World)
	{
		const FSmartObjectClaimHandle ItemHandle = Task.ClaimHandles.Num() > 0 ? Task.ClaimHandles[0] : FSmartObjectClaimHandle::InvalidHandle;
		InstanceData.ActionDuration = ReadActionDuration(World, ItemHandle);
		InstanceData.ActionStartTime = FPlatformTime::Seconds();
	}

	/** 角度差绝对值（0~180 度） */
	float AngularDistanceDegrees(const float FromAngle, const float ToAngle)
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(FromAngle, ToAngle));
	}

	/** 操作枚举名（日志用，如 "EEditMode::HarvestCrop"） */
	FString GetEditModeName(const EEditMode EditMode)
	{
		return StaticEnum<EEditMode>()->GetNameStringByValue(static_cast<int64>(EditMode));
	}
}

EStateTreeRunStatus FSatelliteSelectTargetTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] SelectTarget: satellite data missing (fragment/shared). Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	if (!Shared.PlanetConfig || !Shared.CellManager || !Shared.SmartObjectManager || !Shared.SatelliteManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] SelectTarget: shared references not injected (PlanetConfig/CellManager/SmartObjectManager/SatelliteManager)"));
		return EStateTreeRunStatus::Failed;
	}

	// ω≈0（静止轨道）：不参与作业（无法计算转场速度）——继续巡航
	if (FMath::IsNearlyZero(Orbit.AngularVelocity))
	{
		return EStateTreeRunStatus::Running;
	}

	// 1. 节流：巡航态每 SelectIntervalSeconds 选一次
	FSatelliteSelectTargetTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	const double Now = FPlatformTime::Seconds();
	if (Now - InstanceData.LastSelectTime < SelectIntervalSeconds)
	{
		return EStateTreeRunStatus::Running;
	}
	InstanceData.LastSelectTime = Now;

	const USatelliteManagerComponent& SatelliteManager = *Shared.SatelliteManager;
	const TSet<FIntPoint>& ActiveFarmlands = SatelliteManager.GetActiveFarmlandCoords();
	if (ActiveFarmlands.Num() == 0)
	{
		return EStateTreeRunStatus::Running;
	}

	const FSatelliteScoringWeights& Weights = SatelliteManager.GetScoringWeights();
	const UPlanetConfigDataAsset& PlanetConfig = *Shared.PlanetConfig;
	const FVector OrbitNormal = Shared.OrbitTransform.GetRotation().GetAxisZ();
	const FVector OrbitCenter = Shared.OrbitTransform.GetLocation();

	FOrbitDefinition OrbitData;
	OrbitData.Transform = Shared.OrbitTransform;
	OrbitData.Radius = Shared.Radius;

	// 2. 遍历活跃农田评分：覆盖带硬过滤 → 缺口评估 → 距离扣分 → 取最高分
	FSatelliteTargetCandidate Best;
	for (const FIntPoint& Coord : ActiveFarmlands)
	{
		// 2.1 轨道覆盖硬过滤（管∩球面：格子中心到轨道圆距离 ≤ 服务半径）
		FTransform CellTransform;
		if (!Shared.CellManager->GetCellWorldTransform(Coord, CellTransform))
		{
			continue;
		}
		const FVector CellCenter = CellTransform.GetLocation().GetSafeNormal() * PlanetConfig.Radius;
		if (!UPlanetBlueprintFunctionLibrary::IsPointInOrbitCoverage(CellCenter, OrbitCenter, OrbitNormal, Shared.ServiceDistance))
		{
			continue;
		}

		// 2.2 状态读取（B 路径：直读格子实体 fragment）
		FMassEntityHandle CellEntity;
		if (!Shared.CellManager->GetCellEntity(Coord, CellEntity))
		{
			continue;
		}
		const FPlotStateFragment* PlotState = Data.EntityManager->GetFragmentDataPtr<FPlotStateFragment>(CellEntity);
		if (PlotState == nullptr)
		{
			continue;
		}
		const FCropStateFragment* CropState = Data.EntityManager->GetFragmentDataPtr<FCropStateFragment>(CellEntity);

		// 2.3 缺口收集（缺什么补什么；分数降序——首项为最高分）
		TArray<FCellGap> Gaps;
		if (!CollectCellGaps(*PlotState, CropState, Weights, Gaps))
		{
			continue;
		}
		const EEditMode EditMode = Gaps[0].EditMode;
		const float NeedScore = Gaps[0].Score;

		// 2.4 距离扣分（角度差越小越好）
		const float CellAngle = UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(CellTransform, OrbitData);
		const float Score = NeedScore - AngularDistanceDegrees(Orbit.VirtualPhase, CellAngle) * Weights.DistanceWeight;
		if (Score > Best.Score)
		{
			Best.Coord = Coord;
			Best.EditMode = EditMode;
			Best.Score = Score;
		}
	}

	// 3. 无合适目标（无候选或最高分低于阈值）→ 继续巡航
	if (!Best.IsValid() || Best.Score < Weights.MinScoreThreshold)
	{
		return EStateTreeRunStatus::Running;
	}

	// 4. 写入目标 + 构建作业队列（与选点同源——重新收集该格缺口并按分数降序）
	Task.TargetCoord = Best.Coord;
	Task.TargetEditMode = Best.EditMode;
	Task.WorkQueue.Reset();
	Task.ClaimHandles.Reset();
	Task.ClaimHandle = FSmartObjectClaimHandle::InvalidHandle;

	FMassEntityHandle SelectedEntity;
	if (!Shared.CellManager->GetCellEntity(Best.Coord, SelectedEntity))
	{
		return EStateTreeRunStatus::Running;
	}
	const FPlotStateFragment* SelectedPlot = Data.EntityManager->GetFragmentDataPtr<FPlotStateFragment>(SelectedEntity);
	const FCropStateFragment* SelectedCrop = Data.EntityManager->GetFragmentDataPtr<FCropStateFragment>(SelectedEntity);
	if (SelectedPlot == nullptr)
	{
		return EStateTreeRunStatus::Running;
	}
	TArray<FCellGap> SelectedGaps;
	if (!CollectCellGaps(*SelectedPlot, SelectedCrop, Weights, SelectedGaps))
	{
		// 缺口已消失（本轮被他人完成）：下一轮节流重选
		return EStateTreeRunStatus::Running;
	}
	BuildCellWorkQueue(SelectedGaps, Task.WorkQueue);

	// 5. 认领（整格作业权）：队列涉及的全部 Slot 一次认领（防抢；同 Slot 多项共享句柄；任一项失败则整单回滚）
	TMap<int32, FSmartObjectClaimHandle> SlotHandles;
	bool bClaimAllSucceeded = true;
	for (const TEnumAsByte<EEditMode>& QueueItem : Task.WorkQueue)
	{
		FSmartObjectClaimHandle ItemHandle = FSmartObjectClaimHandle::InvalidHandle;
		const int32 ItemSlotIndex = USmartObjectManagerComponent::GetSlotIndexForEditMode(QueueItem);
		if (ItemSlotIndex >= 0)
		{
			if (const FSmartObjectClaimHandle* Existing = SlotHandles.Find(ItemSlotIndex))
			{
				ItemHandle = *Existing;   // 同 Slot 复用（多个同域操作共享一个 Slot 所有权）
			}
			else
			{
				ItemHandle = Shared.SmartObjectManager->TryClaimSlot(Best.Coord, ItemSlotIndex, SatelliteClaimPriority);
				if (!ItemHandle.IsValid())
				{
					bClaimAllSucceeded = false;
					break;
				}
				SlotHandles.Add(ItemSlotIndex, ItemHandle);
			}
		}
		Task.ClaimHandles.Add(ItemHandle);   // 无需认领的项占位 InvalidHandle（与 WorkQueue 对齐）
	}

	if (!bClaimAllSucceeded)
	{
		// 整单失败：回滚已认领 → 清理 → 下一轮节流重选
		for (const TPair<int32, FSmartObjectClaimHandle>& Pair : SlotHandles)
		{
			Shared.SmartObjectManager->ReleaseClaim(Pair.Value);
		}
		Task.ClaimHandles.Reset();
		Task.WorkQueue.Reset();
		Task.TargetCoord = FIntPoint(-1, -1);
		Task.TargetEditMode = EEditMode::None;
		return EStateTreeRunStatus::Running;
	}

	// 兼容字段：首项句柄（过渡期读取）
	Task.ClaimHandle = Task.ClaimHandles.Num() > 0 ? Task.ClaimHandles[0] : FSmartObjectClaimHandle::InvalidHandle;

	// 卫星状态：待发（离轨时机由 DepartWait 任务判定）
	Orbit.State = ESatelliteState::Depart;

	UE_LOG(LogTemp, Verbose, TEXT("[SatelliteTask] Entity[%d] target cell (%d, %d) action=%s score=%.1f"),
		Data.Entity.Index, Best.Coord.X, Best.Coord.Y, *GetEditModeName(Best.EditMode), Best.Score);

	return EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FSatelliteDepartWaitTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] DepartWait: satellite data missing. Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	if (!Shared.PlanetConfig || !Shared.CellManager || !Shared.SmartObjectManager)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Task.TargetCoord == FIntPoint(-1, -1))
	{
		// 无目标（异常/已被释放）：回巡航
		return EStateTreeRunStatus::Failed;
	}

	// 所有权检查（Depart → Approach 转换点）：失权 → 取消任务（状态树据 Failed 回 Cruise）
	const int32 SlotIndex = USmartObjectManagerComponent::GetSlotIndexForEditMode(Task.TargetEditMode);
	if (SlotIndex >= 0 && !Shared.SmartObjectManager->IsClaimValid(Task.TargetCoord, SlotIndex, Task.ClaimHandle))
	{
		return HandleOwnershipLost(Data, TEXT("DepartWait"));
	}

	// 最近点判据（相位方向与悬停点方位在轨道平面内对齐）——悬停点现算
	FTransform CellTransform;
	if (!Shared.CellManager->GetCellWorldTransform(Task.TargetCoord, CellTransform))
	{
		return EStateTreeRunStatus::Failed;
	}
	const FVector HoverPoint = UPlanetBlueprintFunctionLibrary::ComputeOrbitHoverPoint(
		CellTransform.GetLocation(), Shared.PlanetConfig->Radius, Shared.PlanetConfig->HoverHeight);

	// 时机到 → 出发（不控制运动：卫星照常巡航）
	return UPlanetBlueprintFunctionLibrary::IsAtNearestPhase(Shared.OrbitTransform, Orbit.VirtualPhase, HoverPoint, /*容差=*/2.0f)
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSatelliteReturnWaitTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] ReturnWait: satellite data missing. Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	const FMassOrbitFragment& Orbit = *Data.Orbit;

	// 保持悬停（位置覆盖由作业阶段持有）
	if (!Orbit.bOverrideLocation)
	{
		// 异常：无悬停位置（不应发生）——直接回轨
		return EStateTreeRunStatus::Succeeded;
	}

	// 等到"虚拟相位方向与悬停点方位对齐"（最近点）→ 回轨
	return UPlanetBlueprintFunctionLibrary::IsAtNearestPhase(Shared.OrbitTransform, Orbit.VirtualPhase, Orbit.OverrideLocation, /*容差=*/2.0f)
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSatelliteReturnTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Return: satellite data missing. Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	if (!Shared.SatelliteManager || !Orbit.bOverrideLocation)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 起飞：起点 = 悬停点（当前位置）；终点初值 = 当前虚拟相位点；控制点（回轨：起点法线上升 / 终点切向切入）
	const FVector StartPoint = Orbit.OverrideLocation;
	const FVector InitialEnd = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Shared.OrbitTransform, Shared.Radius, Orbit.VirtualPhase);
	const FVector OrbitTangent = UPlanetBlueprintFunctionLibrary::ComputeOrbitTangent(Shared.OrbitTransform, Orbit.VirtualPhase, Orbit.bClockwise);
	FVector ControlPoint1;
	FVector ControlPoint2;
	UPlanetBlueprintFunctionLibrary::BuildOrbitTransferControlPoints(
		StartPoint, InitialEnd, OrbitTangent, /*bOutbound=*/false, Shared.SatelliteManager->GetTransferControlFactor(), ControlPoint1, ControlPoint2);

	// 时长：路径近似长度 / 轨道线速度（v = ω(rad/s) × R）
	const float PathLength = FVector::Dist(StartPoint, ControlPoint1) + FVector::Dist(ControlPoint1, ControlPoint2) + FVector::Dist(ControlPoint2, InitialEnd);
	const float LinearSpeed = FMath::Abs(FMath::DegreesToRadians(Orbit.AngularVelocity)) * Shared.Radius;
	Task.PhaseStartLocation = StartPoint;
	Task.PhaseEndLocation = InitialEnd;   // 初值（Tick 中动态跟随虚拟相位点）
	Task.PhaseControlPoint1 = ControlPoint1;
	Task.PhaseControlPoint2 = ControlPoint2;
	Task.PhaseElapsed = 0.f;
	Task.PhaseDuration = (LinearSpeed > KINDA_SMALL_NUMBER) ? (PathLength / LinearSpeed) : 1.0f;

	Orbit.State = ESatelliteState::Return;
	// 位置覆盖保持（飞行位置由本任务写入）

	UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] Return start → orbit (phase=%.1f°), duration=%.2fs"),
		Data.Entity.Index, Orbit.VirtualPhase, Task.PhaseDuration);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSatelliteReturnTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Return: satellite data missing. Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	Task.PhaseElapsed += DeltaTime;
	const float Alpha = (Task.PhaseDuration > KINDA_SMALL_NUMBER) ? (Task.PhaseElapsed / Task.PhaseDuration) : 1.f;

	// 动态终点：虚拟相位点（实时）——保证到达时与轨道位置连续（无跳变）
	const FVector DynamicEnd = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Shared.OrbitTransform, Shared.Radius, Orbit.VirtualPhase);
	const FVector OrbitTangent = UPlanetBlueprintFunctionLibrary::ComputeOrbitTangent(Shared.OrbitTransform, Orbit.VirtualPhase, Orbit.bClockwise);
	FVector ControlPoint1;
	FVector ControlPoint2;
	UPlanetBlueprintFunctionLibrary::BuildOrbitTransferControlPoints(
		Task.PhaseStartLocation, DynamicEnd, OrbitTangent, /*bOutbound=*/false,
		Shared.SatelliteManager ? Shared.SatelliteManager->GetTransferControlFactor() : 0.35f, ControlPoint1, ControlPoint2);

	if (Alpha >= 1.f)
	{
		// 到达：位置交还轨道（= 虚拟相位点，与覆盖值连续）→ 恢复巡航
		Orbit.bOverrideLocation = false;
		Orbit.State = ESatelliteState::Cruise;
		Task.PhaseElapsed = 0.f;
		Task.PhaseDuration = 0.f;

		UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] Return complete (phase=%.1f°)"), Data.Entity.Index, Orbit.VirtualPhase);
		return EStateTreeRunStatus::Succeeded;
	}

	Orbit.OverrideLocation = UPlanetBlueprintFunctionLibrary::EvaluateCubicBezier(
		Task.PhaseStartLocation, ControlPoint1, ControlPoint2, DynamicEnd, Alpha);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSatelliteApproachTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Approach: satellite data missing. Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	if (!Shared.PlanetConfig || !Shared.CellManager || !Shared.SatelliteManager)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Task.TargetCoord == FIntPoint(-1, -1))
	{
		return EStateTreeRunStatus::Failed;
	}

	// 起飞：路径参数一次算好（起点=当前轨道位置 / 终点=悬停点 / 控制点 / 时长）
	FTransform CellTransform;
	if (!Shared.CellManager->GetCellWorldTransform(Task.TargetCoord, CellTransform))
	{
		return EStateTreeRunStatus::Failed;
	}
	const FVector StartPoint = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(Shared.OrbitTransform, Shared.Radius, Orbit.VirtualPhase);
	const FVector EndPoint = UPlanetBlueprintFunctionLibrary::ComputeOrbitHoverPoint(
		CellTransform.GetLocation(), Shared.PlanetConfig->Radius, Shared.PlanetConfig->HoverHeight);
	const FVector OrbitTangent = UPlanetBlueprintFunctionLibrary::ComputeOrbitTangent(Shared.OrbitTransform, Orbit.VirtualPhase, Orbit.bClockwise);

	FVector ControlPoint1;
	FVector ControlPoint2;
	UPlanetBlueprintFunctionLibrary::BuildOrbitTransferControlPoints(
		StartPoint, EndPoint, OrbitTangent, /*bOutbound=*/true, Shared.SatelliteManager->GetTransferControlFactor(), ControlPoint1, ControlPoint2);

	// 时长：路径近似长度 / 轨道线速度（v = ω(rad/s) × R）
	const float PathLength = FVector::Dist(StartPoint, ControlPoint1) + FVector::Dist(ControlPoint1, ControlPoint2) + FVector::Dist(ControlPoint2, EndPoint);
	const float LinearSpeed = FMath::Abs(FMath::DegreesToRadians(Orbit.AngularVelocity)) * Shared.Radius;
	Task.PhaseStartLocation = StartPoint;
	Task.PhaseEndLocation = EndPoint;
	Task.PhaseControlPoint1 = ControlPoint1;
	Task.PhaseControlPoint2 = ControlPoint2;
	Task.PhaseElapsed = 0.f;
	Task.PhaseDuration = (LinearSpeed > KINDA_SMALL_NUMBER) ? (PathLength / LinearSpeed) : 1.0f;

	// 不穿球校验（纯算法兜底：失败仅告警，不阻止）
	const float HoverLayerRadius = Shared.PlanetConfig->Radius + Shared.PlanetConfig->HoverHeight;
	if (!UPlanetBlueprintFunctionLibrary::ValidateTransferPath(StartPoint, ControlPoint1, ControlPoint2, EndPoint, HoverLayerRadius))
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Approach: transfer path may intersect planet (cell %d, %d)"), Task.TargetCoord.X, Task.TargetCoord.Y);
	}

	// 开启位置覆盖（离轨期间位置由任务写入）
	Orbit.State = ESatelliteState::Approach;
	Orbit.bOverrideLocation = true;
	Orbit.OverrideLocation = StartPoint;

	UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] Approach start → hover (%.0f, %.0f, %.0f), duration=%.2fs"),
		Data.Entity.Index, EndPoint.X, EndPoint.Y, EndPoint.Z, Task.PhaseDuration);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSatelliteApproachTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Approach: satellite data missing. Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	// 飞行推进（路径参数已一次算好；每帧仅曲线求值）
	Task.PhaseElapsed += DeltaTime;
	const float Alpha = (Task.PhaseDuration > KINDA_SMALL_NUMBER) ? (Task.PhaseElapsed / Task.PhaseDuration) : 1.f;
	if (Alpha < 1.f)
	{
		Orbit.OverrideLocation = UPlanetBlueprintFunctionLibrary::EvaluateCubicBezier(
			Task.PhaseStartLocation, Task.PhaseControlPoint1, Task.PhaseControlPoint2, Task.PhaseEndLocation, Alpha);
		return EStateTreeRunStatus::Running;
	}

	// 到达：精确写悬停点
	Orbit.OverrideLocation = Task.PhaseEndLocation;

	// 到达检查：所有权失 → 已在悬停点（取消任务；状态树据 Failed 跳 AwaitReturn）
	if (Shared.SmartObjectManager != nullptr)
	{
		const int32 SlotIndex = USmartObjectManagerComponent::GetSlotIndexForEditMode(Task.TargetEditMode);
		if (SlotIndex >= 0 && !Shared.SmartObjectManager->IsClaimValid(Task.TargetCoord, SlotIndex, Task.ClaimHandle))
		{
			return HandleOwnershipLost(Data, TEXT("Approach"));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] Approach arrived at cell (%d, %d)"),
		Data.Entity.Index, Task.TargetCoord.X, Task.TargetCoord.Y);

	return EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FSatelliteExecuteActionTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Work: satellite data missing (fragment/shared). Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	if (!Shared.PlanetConfig || !Shared.CellManager || !Shared.SmartObjectManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Work: shared references not injected"));
		return EStateTreeRunStatus::Failed;
	}
	if (Task.TargetCoord == FIntPoint(-1, -1) || Task.WorkQueue.Num() == 0)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 悬停点（现算）→ 写位置覆盖（从任意入口进入均保证悬停在目标格正上方）
	FTransform CellTransform;
	if (Shared.CellManager->GetCellWorldTransform(Task.TargetCoord, CellTransform))
	{
		Orbit.OverrideLocation = UPlanetBlueprintFunctionLibrary::ComputeOrbitHoverPoint(
			CellTransform.GetLocation(), Shared.PlanetConfig->Radius, Shared.PlanetConfig->HoverHeight);
		Orbit.bOverrideLocation = true;
	}
	Orbit.State = ESatelliteState::Work;

	// 认领句柄快照（去重；队列完成时统一释放）
	FSatelliteExecuteActionTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.UniqueClaimHandles.Reset();
	for (const FSmartObjectClaimHandle& ItemHandle : Task.ClaimHandles)
	{
		if (ItemHandle.IsValid() && !InstanceData.UniqueClaimHandles.Contains(ItemHandle))
		{
			InstanceData.UniqueClaimHandles.Add(ItemHandle);
		}
	}

	// 开始首项（读该项行为时长）
	BeginQueueItem(Task, InstanceData, Context.GetWorld());

	UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] work start at cell (%d, %d), items=%d, first=%s duration=%.2fs"),
		Data.Entity.Index, Task.TargetCoord.X, Task.TargetCoord.Y, Task.WorkQueue.Num(),
		*GetEditModeName(Task.WorkQueue[0]), InstanceData.ActionDuration);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSatelliteExecuteActionTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Work: satellite data missing (fragment/shared). Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;
	FSatelliteExecuteActionTaskInstanceData& InstanceData = Context.GetInstanceData(*this);

	// 当前项倒计时未到 → 继续作业停留
	if (FPlatformTime::Seconds() - InstanceData.ActionStartTime < InstanceData.ActionDuration)
	{
		return EStateTreeRunStatus::Running;
	}

	// 执行队首项（AI 入口：播种/重播自动沿用格子已有作物的 CropID）
	const EEditMode CurrentEditMode = Task.WorkQueue[0];
	Shared.CellManager->ExecuteFarmAction(CurrentEditMode, Task.TargetCoord);
	UE_LOG(LogTemp, Verbose, TEXT("[SatelliteTask] Entity[%d] executed action=%s at cell (%d, %d), remaining=%d"),
		Data.Entity.Index, *GetEditModeName(CurrentEditMode), Task.TargetCoord.X, Task.TargetCoord.Y,
		Task.WorkQueue.Num() - 1);

	// 出队（FIFO：队列与句柄数组同步移除首项）
	Task.WorkQueue.RemoveAt(0);
	if (Task.ClaimHandles.Num() > 0)
	{
		Task.ClaimHandles.RemoveAt(0);
	}

	// 队列完成：释放全部认领（去重）→ 清理 → 恢复巡航（过渡：即时回轨；完整回轨动画待后续步骤）
	if (Task.WorkQueue.Num() == 0)
	{
		for (const FSmartObjectClaimHandle& ItemHandle : InstanceData.UniqueClaimHandles)
		{
			Shared.SmartObjectManager->ReleaseClaim(ItemHandle);
		}
		UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] work complete at cell (%d, %d)"),
			Data.Entity.Index, Task.TargetCoord.X, Task.TargetCoord.Y);

		Task.ClaimHandles.Reset();
		Task.ClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
		Task.TargetCoord = FIntPoint(-1, -1);
		Task.TargetEditMode = EEditMode::None;
		// 位置覆盖保持（悬停等待回轨时机）；进入待回状态
		Orbit.State = ESatelliteState::AwaitReturn;
		return EStateTreeRunStatus::Succeeded;
	}

	// 还有下一项：所有权检查（失权 → 取消任务）后开始下一项
	const int32 HeadSlotIndex = USmartObjectManagerComponent::GetSlotIndexForEditMode(Task.WorkQueue[0]);
	if (HeadSlotIndex >= 0)
	{
		const FSmartObjectClaimHandle HeadHandle = Task.ClaimHandles.Num() > 0 ? Task.ClaimHandles[0] : FSmartObjectClaimHandle::InvalidHandle;
		if (!Shared.SmartObjectManager->IsClaimValid(Task.TargetCoord, HeadSlotIndex, HeadHandle))
		{
			return HandleOwnershipLost(Data, TEXT("Work"));
		}
	}
	BeginQueueItem(Task, InstanceData, Context.GetWorld());
	return EStateTreeRunStatus::Running;
}
