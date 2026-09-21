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
		float TargetAngle = 0.f;
		EEditMode EditMode = EEditMode::None;
		float Score = 0.f;

		bool IsValid() const { return Coord != FIntPoint(-1, -1) && EditMode != EEditMode::None; }
	};

	/**
	 * 缺口评估（缺什么补什么）：各缺口按权重计分取最高——权重即优先级（可配）
	 * @return true=存在缺口（OutEditMode/OutScore 有效）
	 */
	bool EvaluateCellNeeds(const FPlotStateFragment& PlotState, const FCropStateFragment* CropState, const FSatelliteScoringWeights& Weights, EEditMode& OutEditMode, float& OutScore)
	{
		OutEditMode = EEditMode::None;
		OutScore = 0.f;

		// 作物缺口：成熟 → 收获；已收获 → 重播（PlantCrop 内部沿用格子已有作物的 CropID）
		if (CropState != nullptr)
		{
			if (CropState->CropGrowthStage == ECropGrowthStage::Mature && Weights.HarvestWeight > OutScore)
			{
				OutScore = Weights.HarvestWeight;
				OutEditMode = EEditMode::HarvestCrop;
			}
			else if (CropState->CropGrowthStage == ECropGrowthStage::Harvested && Weights.ReplantWeight > OutScore)
			{
				OutScore = Weights.ReplantWeight;
				OutEditMode = EEditMode::PlantCrop;
			}
		}

		// 地块缺口：杂草 / 虫害（二值权重）
		if (PlotState.StateData.HasState(EPlotState::WeedInfested) && Weights.WeedWeight > OutScore)
		{
			OutScore = Weights.WeedWeight;
			OutEditMode = EEditMode::UnWeed;
		}
		if (PlotState.StateData.HasState(EPlotState::PestInfested) && Weights.PestWeight > OutScore)
		{
			OutScore = Weights.PestWeight;
			OutEditMode = EEditMode::UnPest;
		}

		// 地块缺口：缺水 / 缺肥（阈值判定 + 连续计分，旧 EQS 公式）
		if (PlotState.MoisturePercentage < Weights.MoistureThreshold)
		{
			const float MoistureScore = (1.f - PlotState.MoisturePercentage) * Weights.MoistureWeight;
			if (MoistureScore > OutScore)
			{
				OutScore = MoistureScore;
				OutEditMode = EEditMode::Wet;
			}
		}
		if (PlotState.FertilityPercentage < Weights.FertilityThreshold)
		{
			const float FertilityScore = (1.f - PlotState.FertilityPercentage) * Weights.FertilityWeight;
			if (FertilityScore > OutScore)
			{
				OutScore = FertilityScore;
				OutEditMode = EEditMode::Fertilize;
			}
		}

		return OutEditMode != EEditMode::None;
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

		// 2.3 缺口评估（缺什么补什么）
		EEditMode EditMode = EEditMode::None;
		float NeedScore = 0.f;
		if (!EvaluateCellNeeds(*PlotState, CropState, Weights, EditMode, NeedScore))
		{
			continue;
		}

		// 2.4 距离扣分（角度差越小越好）
		const float TargetAngle = UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(CellTransform, OrbitData);
		const float Score = NeedScore - AngularDistanceDegrees(Orbit.CurrentAngle, TargetAngle) * Weights.DistanceWeight;
		if (Score > Best.Score)
		{
			Best.Coord = Coord;
			Best.TargetAngle = TargetAngle;
			Best.EditMode = EditMode;
			Best.Score = Score;
		}
	}

	// 3. 无合适目标（无候选或最高分低于阈值）→ 继续巡航
	if (!Best.IsValid() || Best.Score < Weights.MinScoreThreshold)
	{
		return EStateTreeRunStatus::Running;
	}

	// 4. 按操作类型认领对应 Slot（Plot=0 / Crop=1；不适用认领的操作直接执行）
	FSmartObjectClaimHandle ClaimHandle;
	const int32 SlotIndex = USmartObjectManagerComponent::GetSlotIndexForEditMode(Best.EditMode);
	if (SlotIndex >= 0)
	{
		ClaimHandle = Shared.SmartObjectManager->TryClaimSlot(Best.Coord, SlotIndex, SatelliteClaimPriority);
		if (!ClaimHandle.IsValid())
		{
			// 已被其它卫星/玩家占位：跳过（下一轮节流重选）
			return EStateTreeRunStatus::Running;
		}
	}

	// 5. 写入任务数据与移动意图
	Task.ClaimHandle = ClaimHandle;
	Task.TargetCoord = Best.Coord;
	Task.TargetEditMode = Best.EditMode;
	Orbit.MoveState = ESatelliteMoveState::MovingToTarget;
	Orbit.TargetAngle = Best.TargetAngle;

	UE_LOG(LogTemp, Verbose, TEXT("[SatelliteTask] Entity[%d] target cell (%d, %d) action=%s score=%.1f"),
		Data.Entity.Index, Best.Coord.X, Best.Coord.Y, *GetEditModeName(Best.EditMode), Best.Score);

	return EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FSatelliteMoveToTargetTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] MoveToTarget: satellite data missing (fragment/shared). Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}

	// 到达检测：移动处理器到达目标后设置 Paused
	return Data.Orbit->MoveState == ESatelliteMoveState::Paused
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSatelliteExecuteActionTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] ExecuteAction: satellite data missing (fragment/shared). Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	if (!Shared.CellManager || !Shared.SmartObjectManager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] ExecuteAction: CellManager/SmartObjectManager not injected"));
		return EStateTreeRunStatus::Failed;
	}

	// 作业时长：认领有效 → 取智能对象行为定义 Duration（DA_FarmCell）；否则用缺省
	FSatelliteExecuteActionTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	InstanceData.ActionDuration = DefaultActionDuration;
	if (Data.Task->ClaimHandle.IsValid())
	{
		// 注意：GetBehaviorDefinition<T> 为非 const 成员函数，Subsystem 指针不可加 const
		UWorld* World = Context.GetWorld();
		USmartObjectSubsystem* SmartObjectSubsystem = World ? World->GetSubsystem<USmartObjectSubsystem>() : nullptr;
		if (SmartObjectSubsystem != nullptr)
		{
			if (const UFarmActionBehaviorBase* Behavior = SmartObjectSubsystem->GetBehaviorDefinition<UFarmActionBehaviorBase>(Data.Task->ClaimHandle))
			{
				InstanceData.ActionDuration = Behavior->Duration;
			}
		}
	}
	InstanceData.ActionStartTime = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Verbose, TEXT("[SatelliteTask] Entity[%d] working at cell (%d, %d) action=%s duration=%.2fs"),
		Data.Entity.Index, Data.Task->TargetCoord.X, Data.Task->TargetCoord.Y,
		*GetEditModeName(Data.Task->TargetEditMode), InstanceData.ActionDuration);

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FSatelliteExecuteActionTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FSattelliteTaskData Data = GetSatelliteTaskData(Context);
	if (!Data.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] ExecuteAction: satellite data missing (fragment/shared). Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}
	const FMassSatelliteSharedFragment& Shared = *Data.Shared;
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;

	// 作业停留：倒计时未到 → Running
	const FSatelliteExecuteActionTaskInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (FPlatformTime::Seconds() - InstanceData.ActionStartTime < InstanceData.ActionDuration)
	{
		return EStateTreeRunStatus::Running;
	}

	// 执行操作（AI 入口：播种/重播自动沿用格子已有作物的 CropID）
	Shared.CellManager->ExecuteFarmAction(Task.TargetEditMode, Task.TargetCoord);

	// 释放认领（创建类操作无认领句柄时跳过）
	if (Task.ClaimHandle.IsValid())
	{
		Shared.SmartObjectManager->ReleaseClaim(Task.ClaimHandle);
	}

	UE_LOG(LogTemp, Verbose, TEXT("[SatelliteTask] Entity[%d] executed action=%s at cell (%d, %d)"),
		Data.Entity.Index, *GetEditModeName(Task.TargetEditMode), Task.TargetCoord.X, Task.TargetCoord.Y);

	// 清理任务数据并恢复巡航（下一状态选择将从 Cruise 重新开始）
	Task.ClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
	Task.TargetCoord = FIntPoint(-1, -1);
	Task.TargetEditMode = EEditMode::None;
	Orbit.MoveState = ESatelliteMoveState::Idle;

	return EStateTreeRunStatus::Succeeded;
}
