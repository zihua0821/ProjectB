// SatelliteStateTreeTasks.cpp
#include "Mass/SatelliteStateTreeTasks.h"

#include "DrawDebugHelpers.h"

#include "Farm/CellManagerComponent.h"
#include "Farm/FarmlandConfigDataAsset.h"        // 收获后重播停留时长（全局配置）
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
#include "Subsystem/GlobalConfigSubsystem.h"     // 全局配置（农场配置读取）

namespace
{
	/** 卫星认领优先级（玩家 High 可抢占，见《智能对象架构与落地.md》7.5） */
	constexpr ESmartObjectClaimPriority SatelliteClaimPriority = ESmartObjectClaimPriority::Normal;

	/** 起飞时机：离轨提前量点的距离容差（cm；≈0.15°@R=55000，越小越不易"过早/过晚"） */
	constexpr float DepartAlignToleranceCm = 150.0f;

	/** 选点节流间隔（秒；仅巡航态生效，避免每帧遍历候选集） */
	constexpr double SelectIntervalSeconds = 0.2;

	/** 槽位筛选：允许的"落后"上限（度）——限重合 ~ 落后该角度内 */
	constexpr float MaxSlotLagDeg = 45.0f;

	/** 槽位筛选：允许的"超前"容差（度）——仅接受巧合重合，不接受槽位在前 */
	constexpr float MaxSlotLeadDeg = 2.0f;

	/** 起飞时机：槽位接近悬停点投影的容差（度） */
	constexpr float TakeoffAlignToleranceDeg = 3.0f;

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
	bool CollectCellGaps(const FPlotStateFragment& PlotState, const FCropStateFragment* CropState, const FCropGrowthTimerFragment* CropTimer, const float ReplantDelaySeconds, const FSatelliteScoringWeights& Weights, TArray<FCellGap>& OutGaps)
	{
		OutGaps.Reset();

		// 作物缺口：成熟 → 收获（收获后进入 Harvested 阶段，停留时长期满后才可重播）；已收获 → 重播
		if (CropState != nullptr)
		{
			if (CropState->CropGrowthStage == ECropGrowthStage::Mature)
			{
				OutGaps.Add({ EEditMode::HarvestCrop, Weights.HarvestWeight });
			}
			else if (CropState->CropGrowthStage == ECropGrowthStage::Harvested)
			{
				// 收获后停留时长（全局配置）期满才允许重播——保证"收获后"阶段可见
				const float HarvestedElapsed = CropTimer ? CropTimer->CurrentStageGrowthTime : 0.f;
				if (HarvestedElapsed >= ReplantDelaySeconds)
				{
					OutGaps.Add({ EEditMode::PlantCrop, Weights.ReplantWeight });
				}
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

	/**
	 * 开始队列第 ItemIndex 项（重置计时与时长；句柄按"已消费数"索引读取）
	 * 注：ClaimHandles 不再出队（作为出勤账本，保留至回轨到达统一释放），
	 *     当前项句柄 = ClaimHandles[已消费数] = ClaimHandles[总长 − WorkQueue 剩余长度]
	 */
	void BeginQueueItem(const FMassSatelliteTaskFragment& Task, FSatelliteExecuteActionTaskInstanceData& InstanceData, const UWorld* World, const int32 ItemIndex)
	{
		const FSmartObjectClaimHandle ItemHandle = Task.ClaimHandles.IsValidIndex(ItemIndex) ? Task.ClaimHandles[ItemIndex] : FSmartObjectClaimHandle::InvalidHandle;
		InstanceData.ActionDuration = ReadActionDuration(World, ItemHandle);
		InstanceData.ActionStartTime = FPlatformTime::Seconds();
	}

	/** 角度差绝对值（0~180 度） */
	float AngularDistanceDegrees(const float FromAngle, const float ToAngle)
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(FromAngle, ToAngle));
	}

	/**
	 * 悬停点投影 → 槽位 的有向夹角（度；沿运动方向为正）
	 * δ > 0：槽位在悬停点投影【前方】（未到）——回轨会形成追逐，不可用
	 * δ = 0：重合（槽位正处悬停点正下方，最近）
	 * δ < 0：槽位【落后】（刚过投影点）——可用
	 */
	float ComputeHoverToSlotSignedAngle(const FTransform& OrbitTransform, const FVector& HoverPoint, const FVector& SlotPoint, const bool bClockwise)
	{
		const FVector OrbitCenter = OrbitTransform.GetLocation();
		const FVector OrbitNormal = OrbitTransform.GetRotation().GetAxisZ();
		const FVector HoverToCenter = HoverPoint - OrbitCenter;
		const FVector HoverDir = (HoverToCenter - FVector::DotProduct(HoverToCenter, OrbitNormal) * OrbitNormal).GetSafeNormal();
		const FVector SlotToCenter = SlotPoint - OrbitCenter;
		const FVector SlotDir = (SlotToCenter - FVector::DotProduct(SlotToCenter, OrbitNormal) * OrbitNormal).GetSafeNormal();
		if (HoverDir.IsNearlyZero() || SlotDir.IsNearlyZero())
		{
			return 0.f;
		}
		const float CosA = FMath::Clamp(FVector::DotProduct(HoverDir, SlotDir), -1.f, 1.f);
		const float SinA = FVector::DotProduct(FVector::CrossProduct(HoverDir, SlotDir), OrbitNormal);
		const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(SinA, CosA));
		// 沿运动方向为正：顺时针（相位递减）时取反
		return bClockwise ? -AngleDeg : AngleDeg;
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

	// 收获后重播停留时长（全局农场配置；缺失时为 0——立即允许重播）
	float ReplantDelaySeconds = 0.f;
	if (const UWorld* World = Context.GetWorld())
	{
		if (const UGlobalConfigSubsystem* ConfigSub = World->GetSubsystem<UGlobalConfigSubsystem>())
		{
			if (const UFarmlandConfigDataAsset* FarmConfig = ConfigSub->GetFarmlandConfig())
			{
				ReplantDelaySeconds = FarmConfig->ReplantDelaySeconds;
			}
		}
	}
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
		const FCropGrowthTimerFragment* CropTimer = Data.EntityManager->GetFragmentDataPtr<FCropGrowthTimerFragment>(CellEntity);

		// 2.3 缺口收集（缺什么补什么；分数降序——首项为最高分）
		TArray<FCellGap> Gaps;
		if (!CollectCellGaps(*PlotState, CropState, CropTimer, ReplantDelaySeconds, Weights, Gaps))
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
	const FCropGrowthTimerFragment* SelectedCropTimer = Data.EntityManager->GetFragmentDataPtr<FCropGrowthTimerFragment>(SelectedEntity);
	if (SelectedPlot == nullptr)
	{
		return EStateTreeRunStatus::Running;
	}
	TArray<FCellGap> SelectedGaps;
	if (!CollectCellGaps(*SelectedPlot, SelectedCrop, SelectedCropTimer, ReplantDelaySeconds, Weights, SelectedGaps))
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

	// 时机判据（方案 2）：等"当前方位 = θ_hover − ω·T·dir"（精确提前量）
	// —— 与 Approach 起飞起点同一公式，保证起飞瞬间无位置跳变
	// 时机（与 Approach/Return 同参数：T = ΔR / v_r）
	const float RadialSpeed = FMath::Max(Shared.SatelliteManager ? Shared.SatelliteManager->GetRadialTransferSpeed() : 2000.0f, 1.0f);
	const float TravelDirection = Orbit.bClockwise ? -1.0f : 1.0f;

	// 悬停点分解 + 合成跨度（与 Approach 同公式：含垂直分量）
	float ThetaHoverDeg = 0.0f;
	float RadiusHoverInPlane = 0.0f;
	float VerticalHover = 0.0f;
	UPlanetBlueprintFunctionLibrary::DecomposeToOrbitPlane(Shared.OrbitTransform, HoverPoint, ThetaHoverDeg, RadiusHoverInPlane, VerticalHover);
	const float RadSpan = FMath::Sqrt(FMath::Square(RadiusHoverInPlane - Shared.Radius) + FMath::Square(VerticalHover));
	const float Duration = FMath::Max(RadSpan / RadialSpeed, 0.1f);
	const float ThetaStartDeg = ThetaHoverDeg - Orbit.AngularVelocity * Duration * TravelDirection;

	// 提前量点的世界位置（轨迹起点）
	const FVector StartPointWorld = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
		Shared.OrbitTransform, Shared.Radius, ThetaStartDeg);

	// 判据（关键）：用卫星的【显示位置】与提前量点的距离
	// —— 方案 B 下显示位置 = 归属槽世界点（不是账面相位点），只有判它才能保证起飞无位置跳变
	FVector DisplayLocation = StartPointWorld;
	if (Shared.SmartObjectManager->GetOrbitSlotWorldPositionByClaim(Orbit.OrbitName, Orbit.SlotClaimHandle, DisplayLocation))
	{
		// 用归属槽世界点（与渲染同源）
	}
	else
	{
		// 回退：无槽时用账面相位点
		DisplayLocation = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
			Shared.OrbitTransform, Shared.Radius, Orbit.VirtualPhase);
	}

	const float DistToStart = FVector::Dist(DisplayLocation, StartPointWorld);

	return (DistToStart <= DepartAlignToleranceCm)
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
	FMassOrbitFragment& Orbit = *Data.Orbit;
	FMassSatelliteTaskFragment& Task = *Data.Task;
	FSatelliteReturnWaitTaskInstanceData& InstanceData = Context.GetInstanceData(*this);

	if (!Shared.SmartObjectManager || !Shared.SatelliteManager || !Shared.PlanetConfig)
	{
		return EStateTreeRunStatus::Failed;
	}
	// 保持悬停（位置覆盖由作业阶段持有）
	if (!Orbit.bOverrideLocation)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// 选槽（节流 0.2s）：空槽 → 三条件（不反向 / 相位差 ≥ MinPhaseGap）→ 按顺向相位差升序逐个认领
	// 选中后：句柄写 SlotClaimHandle，槽位点写 PhaseEndLocation（供近点判定与回轨终点复用）
	const double Now = FPlatformTime::Seconds();
	if (!Orbit.SlotClaimHandle.IsValid() && Now - InstanceData.LastSelectTime >= SelectIntervalSeconds)
	{
		InstanceData.LastSelectTime = Now;

		const int32 SlotCount = Shared.SmartObjectManager->GetOrbitSlotCount(Orbit.OrbitName);

		// 收集候选（顺向相位差 = 沿 ω 方向从当前相位到槽相位的角度）
		struct FSlotCandidate
		{
			int32 SlotIndex = -1;
			float SlotDeltaDeg = 0.f;   // 悬停点投影→槽位 的有向夹角（沿运动方向为正）
		};
		TArray<FSlotCandidate> Candidates;
		Candidates.Reserve(SlotCount);
		for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
		{
			// 槽位世界点
			FVector SlotWorld;
			if (!Shared.SmartObjectManager->GetOrbitSlotWorldPosition(Orbit.OrbitName, SlotIndex, SlotWorld))
			{
				continue;
			}
			// 槽位相位（当前方案 2 下不再参与路径构造；保留查询以兼容槽位索引校验）
			float SlotPhaseDeg = 0.f;
			if (!Shared.SmartObjectManager->GetOrbitSlotPhase(Orbit.OrbitName, SlotIndex, SlotPhaseDeg))
			{
				continue;
			}

			// 可用性：悬停点投影 → 槽位 的有向夹角 δ（沿运动方向为正）
			// 仅接受"重合 ~ 落后 MaxSlotLagDeg"（δ ∈ [−45°, +2°]）；槽位在前（δ>0）会形成追逐 → 排除
			const float SlotDeltaDeg = ComputeHoverToSlotSignedAngle(
				Shared.OrbitTransform, Orbit.OverrideLocation, SlotWorld, Orbit.bClockwise);
			if (SlotDeltaDeg > MaxSlotLeadDeg || SlotDeltaDeg < -MaxSlotLagDeg)
			{
				continue;
			}

			Candidates.Add({ SlotIndex, SlotDeltaDeg });
		}
		// 按"离重合的远近"升序（越接近重合越优先——即槽位离悬停点越近）
		Candidates.Sort([](const FSlotCandidate& A, const FSlotCandidate& B) { return FMath::Abs(A.SlotDeltaDeg) < FMath::Abs(B.SlotDeltaDeg); });

		// 逐个尝试认领（空槽 = 认领成功；失败顺延下一候选）
		for (const FSlotCandidate& Candidate : Candidates)
		{
			const FSmartObjectClaimHandle ClaimHandle = Shared.SmartObjectManager->TryClaimOrbitSlot(
				Orbit.OrbitName, Candidate.SlotIndex, SatelliteClaimPriority);
			if (ClaimHandle.IsValid())
			{
				Orbit.SlotClaimHandle = ClaimHandle;
				// 槽位世界位置写入飞行终点（起飞时机判定/回轨终点共用）
				Shared.SmartObjectManager->GetOrbitSlotWorldPosition(Orbit.OrbitName, Candidate.SlotIndex, Task.PhaseEndLocation);
				UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] ReturnWait: slot %d selected (delta=%.1f°)"),
					Data.Entity.Index, Candidate.SlotIndex, Candidate.SlotDeltaDeg);
				break;
			}
		}
	}

	// 未选中：继续等待（相位在动，下个节流窗口重试；诊断在函数开头无条件输出）
	if (!Orbit.SlotClaimHandle.IsValid())
	{
		return EStateTreeRunStatus::Running;
	}

	// 等到"槽位【实时】转到悬停点投影处"（δ ≈ 0）→ 起飞
	// 注意：必须用槽位的当前世界位置（槽位在公转），不能用选槽时的快照点
	FVector SlotWorldNow;
	if (!Shared.SmartObjectManager->GetOrbitSlotWorldPositionByClaim(Orbit.OrbitName, Orbit.SlotClaimHandle, SlotWorldNow))
	{
		return EStateTreeRunStatus::Running;
	}
	const float SlotDeltaNow = ComputeHoverToSlotSignedAngle(
		Shared.OrbitTransform, Orbit.OverrideLocation, SlotWorldNow, Orbit.bClockwise);
	return (FMath::Abs(SlotDeltaNow) <= TakeoffAlignToleranceDeg)
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

	if (!Shared.SatelliteManager || !Shared.PlanetConfig || !Orbit.bOverrideLocation)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 起点 = 悬停点；落点 = θ_hover + ω·T·dir（切向同步，天然对准；无需预测）
	const FVector StartPoint = Orbit.OverrideLocation;
	if (!Orbit.SlotClaimHandle.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SatelliteTask] Return: no slot claimed. Entity[%d]"), Data.Entity.Index);
		return EStateTreeRunStatus::Failed;
	}

	// 时长 T = ΔR / v_r（单参数，配置于卫星管理组件；与离轨共用）
	const float RadiusOrbit = Shared.Radius;
	const float RadialSpeed = FMath::Max(Shared.SatelliteManager->GetRadialTransferSpeed(), 1.0f);
	const float TravelDirection = Orbit.bClockwise ? -1.0f : 1.0f;

	// 权威点分解（悬停点 → 平面内方位/半径 + 垂直偏移；垂直分量不可省）
	float ThetaHoverDeg = 0.0f;
	float RadiusHoverInPlane = 0.0f;
	float VerticalHover = 0.0f;
	UPlanetBlueprintFunctionLibrary::DecomposeToOrbitPlane(Shared.OrbitTransform, StartPoint, ThetaHoverDeg, RadiusHoverInPlane, VerticalHover);

	// 时长：径向合成跨度（平面内 + 垂直）/ 径向速度
	const float RadSpan = FMath::Sqrt(FMath::Square(RadiusOrbit - RadiusHoverInPlane) + FMath::Square(VerticalHover));
	const float Duration = FMath::Max(RadSpan / RadialSpeed, 0.1f);

	// 切向推进量 = ω·T·dir + δ0（δ0 补偿）
	// —— 槽位与卫星同速，起飞时刻的偏差 δ0 会被"冻结"到落点；
	//    用 δ0 补偿后：落点 = 槽位到达时的相位 → 完全重合
	float ThetaDeltaDeg = Orbit.AngularVelocity * Duration * TravelDirection;
	if (Orbit.SlotClaimHandle.IsValid())
	{
		float SlotPhaseDeg = 0.f;
		if (Shared.SmartObjectManager->GetOrbitSlotPhaseByClaim(Orbit.OrbitName, Orbit.SlotClaimHandle, SlotPhaseDeg))
		{
			// 槽位相对悬停点投影方位的偏差（沿运动方向为正；归一到 [−180,180]）
			const float SlotOffsetDeg = FMath::FindDeltaAngleDegrees(ThetaHoverDeg, SlotPhaseDeg) * TravelDirection;
			ThetaDeltaDeg += SlotOffsetDeg;
		}
	}

	Task.PhaseStartLocation = StartPoint;
	Task.PhaseThetaStartDeg = ThetaHoverDeg;
	Task.PhaseThetaDeltaDeg = ThetaDeltaDeg;
	Task.PhaseRadiusStart = RadiusHoverInPlane;
	Task.PhaseRadiusEnd = RadiusOrbit;
	Task.PhaseVerticalStart = VerticalHover;
	Task.PhaseVerticalEnd = 0.0f;
	Task.PhaseEndLocation = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
		Shared.OrbitTransform, RadiusOrbit, ThetaHoverDeg + ThetaDeltaDeg);
	Task.PhaseElapsed = 0.f;
	Task.PhaseDuration = Duration;

	Orbit.State = ESatelliteState::Return;
	// 位置覆盖保持（飞行位置由本任务写入）

	UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] Return start → orbit (phase=%.1f°), duration=%.2fs"),
		Data.Entity.Index, Orbit.VirtualPhase, Task.PhaseDuration);

	// 路径点调试（参数方程采样：青）
	if (UWorld* World = Context.GetWorld())
	{
		constexpr int32 PathSampleCount = 80;   // 采样密度（间距 ≈ 弧长/80 ≈ 50cm）
		for (int32 SampleIndex = 0; SampleIndex <= PathSampleCount; ++SampleIndex)
		{
			const float SampleAlpha = static_cast<float>(SampleIndex) / static_cast<float>(PathSampleCount);
			const float SampleTheta = Task.PhaseThetaStartDeg + Task.PhaseThetaDeltaDeg * SampleAlpha;
			const float SampleRadius = FMath::Lerp(Task.PhaseRadiusStart, Task.PhaseRadiusEnd, SampleAlpha);
			const float SampleVertical = FMath::Lerp(Task.PhaseVerticalStart, Task.PhaseVerticalEnd, SampleAlpha);
			const FVector PathPoint = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
				Shared.OrbitTransform, SampleRadius, SampleTheta) + Shared.OrbitTransform.GetRotation().GetAxisZ() * SampleVertical;
			DrawDebugSphere(World, PathPoint, 25.f, 6, FColor::Cyan, /*bPersistent=*/false, /*LifeTime=*/10.f, /*DepthPriority=*/0, /*Thickness=*/0.f);
		}
	}

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

	if (Alpha >= 1.f)
	{
		// 到达：位置 = 落点（与槽位/虚拟相位一致）→ 清覆盖（无瞬移）
		Orbit.OverrideLocation = Task.PhaseEndLocation;
		Orbit.bOverrideLocation = false;

		Orbit.State = ESatelliteState::Cruise;
		Task.PhaseElapsed = 0.f;
		Task.PhaseDuration = 0.f;
		// 释放格子认领（唯一释放点：真实回轨到达；同句柄去重）
		if (Shared.SmartObjectManager)
		{
			TArray<FSmartObjectClaimHandle> UniqueHandles;
			for (const FSmartObjectClaimHandle& ItemHandle : Task.ClaimHandles)
			{
				if (ItemHandle.IsValid() && !UniqueHandles.Contains(ItemHandle))
				{
					UniqueHandles.Add(ItemHandle);
					Shared.SmartObjectManager->ReleaseClaim(ItemHandle);
				}
			}
		}
		Task.ClaimHandles.Reset();
		Task.ClaimHandle = FSmartObjectClaimHandle::InvalidHandle;

		UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] Return complete (phase=%.1f°)"),
			Data.Entity.Index, Orbit.VirtualPhase);

		// 归位（防止"清覆盖后位置跳变"）：
		// ① 账面相位与槽位对齐（VirtualPhase := 槽位相位）——保证无槽回退时也不跳
		// ② 【保留】归属槽认领句柄（SlotClaimHandle）——渲染锚点继续用槽位点，不回退到相位点
		if (Shared.SmartObjectManager)
		{
			float SlotPhaseDeg = 0.f;
			if (Shared.SmartObjectManager->GetOrbitSlotPhaseByClaim(Orbit.OrbitName, Orbit.SlotClaimHandle, SlotPhaseDeg))
			{
				Orbit.VirtualPhase = SlotPhaseDeg;
			}
		}
		return EStateTreeRunStatus::Succeeded;
	}

	// 极坐标参数方程（含垂直分量）：θ 同步推进 + 平面内半径线性插值 + 法线偏移线性插值
	const float ThetaDeg = Task.PhaseThetaStartDeg + Task.PhaseThetaDeltaDeg * Alpha;
	const float RadiusNow = FMath::Lerp(Task.PhaseRadiusStart, Task.PhaseRadiusEnd, Alpha);
	const float VerticalNow = FMath::Lerp(Task.PhaseVerticalStart, Task.PhaseVerticalEnd, Alpha);
	const FVector PlanePoint = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
		Shared.OrbitTransform, RadiusNow, ThetaDeg);
	Orbit.OverrideLocation = PlanePoint + Shared.OrbitTransform.GetRotation().GetAxisZ() * VerticalNow;

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

	// 起飞：极坐标参数方程参数（方案 2：切向同步 ω + 径向匀速 v_r；一次算好）
	FTransform CellTransform;
	if (!Shared.CellManager->GetCellWorldTransform(Task.TargetCoord, CellTransform))
	{
		return EStateTreeRunStatus::Failed;
	}
	const FVector EndPoint = UPlanetBlueprintFunctionLibrary::ComputeOrbitHoverPoint(
		CellTransform.GetLocation(), Shared.PlanetConfig->Radius, Shared.PlanetConfig->HoverHeight);

	// 时长 T = ΔR / v_r（单参数，配置于卫星管理组件；离轨/回轨共用）
	const float RadiusOrbit = Shared.Radius;
	const float RadialSpeed = FMath::Max(Shared.SatelliteManager ? Shared.SatelliteManager->GetRadialTransferSpeed() : 2000.0f, 1.0f);
	const float TravelDirection = Orbit.bClockwise ? -1.0f : 1.0f;

	// 起点位置（关键）：取卫星【显示位置】——从哪儿起就从哪儿算，保证起飞帧零跳变
	FVector DisplayLocation;
	if (Shared.SmartObjectManager->GetOrbitSlotWorldPositionByClaim(Orbit.OrbitName, Orbit.SlotClaimHandle, DisplayLocation))
	{
		// 用归属槽世界点（与渲染同源）
	}
	else
	{
		DisplayLocation = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
			Shared.OrbitTransform, RadiusOrbit, Orbit.VirtualPhase);
	}

	// 权威点分解（悬停点 → 平面内方位/半径 + 垂直偏移；垂直分量不可省：服务距离可能远大于平面）
	float ThetaHoverDeg = 0.0f;
	float RadiusHoverInPlane = 0.0f;
	float VerticalHover = 0.0f;
	UPlanetBlueprintFunctionLibrary::DecomposeToOrbitPlane(Shared.OrbitTransform, EndPoint, ThetaHoverDeg, RadiusHoverInPlane, VerticalHover);

	float ThetaStartDeg = 0.0f;
	float RadiusStartInPlane = RadiusOrbit;
	float VerticalStart = 0.0f;
	UPlanetBlueprintFunctionLibrary::DecomposeToOrbitPlane(Shared.OrbitTransform, DisplayLocation, ThetaStartDeg, RadiusStartInPlane, VerticalStart);

	// 时长：径向合成跨度（平面内分量 + 垂直分量）/ 径向速度
	const float RadSpan = FMath::Sqrt(FMath::Square(RadiusHoverInPlane - RadiusStartInPlane) + FMath::Square(VerticalHover - VerticalStart));
	const float Duration = FMath::Max(RadSpan / RadialSpeed, 0.1f);

	Task.PhaseStartLocation = DisplayLocation;
	Task.PhaseEndLocation = EndPoint;
	Task.PhaseThetaStartDeg = ThetaStartDeg;
	Task.PhaseThetaDeltaDeg = Orbit.AngularVelocity * Duration * TravelDirection;
	Task.PhaseRadiusStart = RadiusStartInPlane;
	Task.PhaseRadiusEnd = RadiusHoverInPlane;
	Task.PhaseVerticalStart = VerticalStart;
	Task.PhaseVerticalEnd = VerticalHover;
	Task.PhaseElapsed = 0.f;
	Task.PhaseDuration = Duration;

	// 让槽（释放槽位归属；回轨时重新认领空槽）
	if (Shared.SmartObjectManager && Orbit.SlotClaimHandle.IsValid())
	{
		Shared.SmartObjectManager->ReleaseClaim(Orbit.SlotClaimHandle);
		Orbit.SlotClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
	}

	// 开启位置覆盖（离轨期间位置由任务写入）
	Orbit.State = ESatelliteState::Approach;
	Orbit.bOverrideLocation = true;
	Orbit.OverrideLocation = Task.PhaseStartLocation;

	// 路径点调试（参数方程采样：黄）
	if (UWorld* World = Context.GetWorld())
	{
		constexpr int32 PathSampleCount = 80;   // 采样密度（间距 ≈ 弧长/80 ≈ 50cm）
		for (int32 SampleIndex = 0; SampleIndex <= PathSampleCount; ++SampleIndex)
		{
			const float SampleAlpha = static_cast<float>(SampleIndex) / static_cast<float>(PathSampleCount);
			const float SampleTheta = Task.PhaseThetaStartDeg + Task.PhaseThetaDeltaDeg * SampleAlpha;
			const float SampleRadius = FMath::Lerp(Task.PhaseRadiusStart, Task.PhaseRadiusEnd, SampleAlpha);
			const float SampleVertical = FMath::Lerp(Task.PhaseVerticalStart, Task.PhaseVerticalEnd, SampleAlpha);
			const FVector PathPoint = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
				Shared.OrbitTransform, SampleRadius, SampleTheta) + Shared.OrbitTransform.GetRotation().GetAxisZ() * SampleVertical;
			DrawDebugSphere(World, PathPoint, 25.f, 6, FColor::Yellow, /*bPersistent=*/false, /*LifeTime=*/10.f, /*DepthPriority=*/0, /*Thickness=*/0.f);
		}
	}

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

	// 飞行推进（参数已一次算好；每帧仅参数方程求值）
	Task.PhaseElapsed += DeltaTime;
	const float Alpha = (Task.PhaseDuration > KINDA_SMALL_NUMBER) ? (Task.PhaseElapsed / Task.PhaseDuration) : 1.f;
	if (Alpha < 1.f)
	{
		// 极坐标参数方程（含垂直分量）：θ 同步推进 + 平面内半径线性插值 + 法线偏移线性插值
		const float ThetaDeg = Task.PhaseThetaStartDeg + Task.PhaseThetaDeltaDeg * Alpha;
		const float RadiusNow = FMath::Lerp(Task.PhaseRadiusStart, Task.PhaseRadiusEnd, Alpha);
		const float VerticalNow = FMath::Lerp(Task.PhaseVerticalStart, Task.PhaseVerticalEnd, Alpha);
		const FVector PlanePoint = UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(
			Shared.OrbitTransform, RadiusNow, ThetaDeg);
		const FVector OrbitNormal = Shared.OrbitTransform.GetRotation().GetAxisZ();
		Orbit.OverrideLocation = PlanePoint + OrbitNormal * VerticalNow;

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

	FSatelliteExecuteActionTaskInstanceData& InstanceData = Context.GetInstanceData(*this);

	// 开始首项（读该项行为时长；此时已消费数 = 0）
	BeginQueueItem(Task, InstanceData, Context.GetWorld(), 0);

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

	// 出队（FIFO：只消耗操作队列；句柄不清——全量保留作为"出勤账本"，回轨到达时统一释放）
	Task.WorkQueue.RemoveAt(0);

	// 队列完成：不释放格子认领（保留至真实回轨到达，期间对他星保持互斥）→ 进入待回
	if (Task.WorkQueue.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] work complete at cell (%d, %d)"),
			Data.Entity.Index, Task.TargetCoord.X, Task.TargetCoord.Y);
	
		Task.ClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
		Task.TargetCoord = FIntPoint(-1, -1);
		Task.TargetEditMode = EEditMode::None;
		// 位置覆盖保持（悬停等待回轨时机）；进入待回状态
		Orbit.State = ESatelliteState::AwaitReturn;
		return EStateTreeRunStatus::Succeeded;
	}

	// 还有下一项：所有权检查（失权 → 取消任务）后开始下一项
	// 已消费数 = 总句柄数 − 剩余队列长度（当前项 = WorkQueue[0] ↔ ClaimHandles[已消费数]）
	const int32 ConsumedCount = Task.ClaimHandles.Num() - Task.WorkQueue.Num();
	const FSmartObjectClaimHandle HeadHandle = Task.ClaimHandles.IsValidIndex(ConsumedCount) ? Task.ClaimHandles[ConsumedCount] : FSmartObjectClaimHandle::InvalidHandle;
	const int32 HeadSlotIndex = USmartObjectManagerComponent::GetSlotIndexForEditMode(Task.WorkQueue[0]);
	if (HeadSlotIndex >= 0)
	{
		if (!Shared.SmartObjectManager->IsClaimValid(Task.TargetCoord, HeadSlotIndex, HeadHandle))
		{
			return HandleOwnershipLost(Data, TEXT("Work"));
		}
	}
	BeginQueueItem(Task, InstanceData, Context.GetWorld(), ConsumedCount);
	return EStateTreeRunStatus::Running;
}
