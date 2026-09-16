// SatelliteTaskProcessor.cpp
#include "Mass/SatelliteTaskProcessor.h"

#include "Farm/CellManagerComponent.h"
#include "Mass/CellMassFragments.h"     // FCropStateFragment / ECropGrowthStage
#include "Mass/SatelliteMassFragments.h"
#include "Mass/SmartObjectManagerComponent.h"
#include "MassExecutionContext.h"
#include "Planet/OrbitManagerComponent.h"          // FOrbitData
#include "Planet/PlanetBlueprintFunctionLibrary.h"
#include "Planet/PlanetConfigDataAsset.h"

namespace
{
	/** 智能对象 Slot 索引约定：0=Plot（地块），1=Crop（作物） */
	constexpr int32 CropSlotIndex = 1;

	/** 卫星认领优先级（玩家 High 可抢占，见《智能对象架构与落地.md》7.5） */
	constexpr ESmartObjectClaimPriority SatelliteClaimPriority = ESmartObjectClaimPriority::Normal;

	/**
	 * 巡航检测：最近格子作物成熟 → 立即认领（移动前占位）→ 设定移动目标
	 */
	void TryAcquireTarget(FMassEntityManager& EntityManager, const FMassSatelliteSharedFragment& Shared,
		FMassOrbitFragment& Orbit, FMassSatelliteTaskFragment& Task, const FMassEntityHandle Entity)
	{
		if (!Shared.PlanetConfig || !Shared.CellManager || !Shared.SmartObjectManager)
		{
			return;
		}

		// 1. 卫星当前位置 → 最近格子
		// 球面几何反推：位置方向投影到星球表面点，复用网格生成公式的逆运算
		const FVector WorldLocation = ComputeOrbitWorldPosition(Shared.OrbitTransform, Shared.Radius, Orbit.CurrentAngle);
		const FVector SurfacePoint = WorldLocation.GetSafeNormal() * Shared.PlanetConfig->Radius;
		const FIntPoint CellCoord = UPlanetBlueprintFunctionLibrary::ConvertLocationToCellCoord(
			SurfacePoint, Shared.PlanetConfig, /*IsWorldLocation=*/false);

		// 2. 状态筛选：作物成熟才操作（B 路径：直接读格子实体 fragment，不走引擎查询）
		FMassEntityHandle CellEntity;
		if (!Shared.CellManager->GetCellEntity(CellCoord, CellEntity))
		{
			return;
		}
		const FCropStateFragment* CropState = EntityManager.GetFragmentDataPtr<FCropStateFragment>(CellEntity);
		if (CropState == nullptr || CropState->CropGrowthStage != ECropGrowthStage::Mature)
		{
			return;
		}

		// 3. 选点后立即认领（移动前占位，避免多卫星同抢同一格子）
		const FSmartObjectClaimHandle ClaimHandle = Shared.SmartObjectManager->TryClaimSlot(
			CellCoord, CropSlotIndex, SatelliteClaimPriority);
		if (!ClaimHandle.IsValid())
		{
			// 已被其它卫星认领 / Slot 未启用：跳过（下一帧最近格子可能已变化）
			return;
		}

		// 4. 计算目标角度（格子中心角度）并进入移动状态
		FTransform CellTransform;
		if (!Shared.CellManager->GetCellWorldTransform(CellCoord, CellTransform))
		{
			Shared.SmartObjectManager->ReleaseClaim(ClaimHandle);
			return;
		}

		FOrbitData OrbitData;
		OrbitData.Transform = Shared.OrbitTransform;
		OrbitData.Radius = Shared.Radius;

		Task.ClaimHandle = ClaimHandle;
		Task.TargetCoord = CellCoord;
		Task.TaskState = ESatelliteTaskState::MovingToTarget;
		Orbit.MoveState = ESatelliteMoveState::MovingToTarget;
		Orbit.TargetAngle = UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(CellTransform, OrbitData);

		UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] claimed cell (%d, %d) for harvest"),
			Entity.Index, CellCoord.X, CellCoord.Y);
	}

	/**
	 * 前往目标：到达（移动处理器已置 Paused）→ 执行收获 → 释放 → 恢复巡航
	 */
	void TryExecuteTask(const FMassSatelliteSharedFragment& Shared,
		FMassOrbitFragment& Orbit, FMassSatelliteTaskFragment& Task, const FMassEntityHandle Entity)
	{
		// 到达检测：移动处理器到达目标后设置 Paused
		if (Orbit.MoveState != ESatelliteMoveState::Paused)
		{
			return;
		}

		if (!Shared.CellManager || !Shared.SmartObjectManager)
		{
			return;
		}

		// 执行收获（复用玩家同一操作入口；成功/失败均释放，统一重选出口）
		Shared.CellManager->HandleLeftMouseClicked(EEditMode::HarvestCrop, Task.TargetCoord);

		// 释放认领
		Shared.SmartObjectManager->ReleaseClaim(Task.ClaimHandle);

		UE_LOG(LogTemp, Log, TEXT("[SatelliteTask] Entity[%d] harvested cell (%d, %d)"),
			Entity.Index, Task.TargetCoord.X, Task.TargetCoord.Y);

		// 返回巡航（继续沿轨道巡视）
		Task.ClaimHandle = FSmartObjectClaimHandle::InvalidHandle;
		Task.TargetCoord = FIntPoint(-1, -1);
		Task.TaskState = ESatelliteTaskState::Cruising;
		Orbit.MoveState = ESatelliteMoveState::Idle;
	}
}

USatelliteTaskProcessor::USatelliteTaskProcessor()
	: EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	// 访问 SmartObjectSubsystem / CellManager，游戏线程执行
	bRequiresGameThreadExecution = true;
	// 执行组：移动推进与渲染同步按此组名排在其后（同帧读到最新目标/状态）
	ExecutionOrder.ExecuteInGroup = TEXT("SatelliteTask");
}

void USatelliteTaskProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassOrbitFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassSatelliteTaskFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FMassSatelliteSharedFragment>();
}

void USatelliteTaskProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntityManager, Context, [&EntityManager](FMassExecutionContext& ChunkContext)
	{
		const TArrayView<FMassOrbitFragment> OrbitFragments = ChunkContext.GetMutableFragmentView<FMassOrbitFragment>();
		const TArrayView<FMassSatelliteTaskFragment> TaskFragments = ChunkContext.GetMutableFragmentView<FMassSatelliteTaskFragment>();
		const FMassSatelliteSharedFragment& Shared = ChunkContext.GetConstSharedFragment<FMassSatelliteSharedFragment>();
		const int32 NumEntities = ChunkContext.GetNumEntities();

		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			FMassOrbitFragment& Orbit = OrbitFragments[Index];
			FMassSatelliteTaskFragment& Task = TaskFragments[Index];

			switch (Task.TaskState)
			{
			case ESatelliteTaskState::Cruising:
				TryAcquireTarget(EntityManager, Shared, Orbit, Task, ChunkContext.GetEntity(Index));
				break;

			case ESatelliteTaskState::MovingToTarget:
				TryExecuteTask(Shared, Orbit, Task, ChunkContext.GetEntity(Index));
				break;

			default:
				break;
			}
		}
	});
}
