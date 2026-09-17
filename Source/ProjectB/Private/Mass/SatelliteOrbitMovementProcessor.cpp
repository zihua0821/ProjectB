// SatelliteOrbitMovementProcessor.cpp
#include "Mass/SatelliteOrbitMovementProcessor.h"

#include "MassExecutionContext.h"
#include "Mass/SatelliteMassFragments.h"

namespace
{
	/**
	 * 单卫星角度推进（迁移自 UOrbitMovementComponent::UpdateOrbitMovement）
	 */
	void UpdateOrbitMovement(FMassOrbitFragment& Orbit, const float DeltaTime)
	{
		// 零角速度：不移动
		if (FMath::IsNearlyZero(Orbit.AngularVelocity))
		{
			return;
		}

		float NewAngle = Orbit.CurrentAngle;
		const float DeltaAngle = Orbit.AngularVelocity * DeltaTime;

		switch (Orbit.MoveState)
		{
		case ESatelliteMoveState::Idle:
			NewAngle += (Orbit.bClockwise ? -DeltaAngle : DeltaAngle);
			break;

		case ESatelliteMoveState::MovingToTarget:
			{
				// 计算最短弧方向（归一化到 [-180, 180]）
				float Delta = Orbit.TargetAngle - Orbit.CurrentAngle;
				Delta = FMath::Fmod(Delta + 180.0f, 360.0f);
				if (Delta < 0.0f)
				{
					Delta += 360.0f;
				}
				Delta -= 180.0f;

				if (FMath::Abs(Delta) <= 0.1f)
				{
					// 到达目标（到达事件后续由 MassSignals 广播，阶段 6 决策消费）
					NewAngle = Orbit.TargetAngle;
					Orbit.MoveState = ESatelliteMoveState::Paused;
				}
				else
				{
					// 向目标方向移动一步，不越过目标
					const float Step = FMath::Min(FMath::Abs(Delta), DeltaAngle);
					NewAngle += (Delta > 0.0f ? Step : -Step);
				}
			}
			break;

		case ESatelliteMoveState::Paused:
			return;

		default:
			break;
		}

		// 归一化角度到 [0, 360)
		NewAngle = FMath::Fmod(NewAngle, 360.0f);
		if (NewAngle < 0.0f)
		{
			NewAngle += 360.0f;
		}
		Orbit.CurrentAngle = NewAngle;
	}
}

USatelliteOrbitMovementProcessor::USatelliteOrbitMovementProcessor()
	: EntityQuery(*this)
{
	// 注册到默认处理阶段自动调用（与项目现有处理器一致）
	bAutoRegisterWithProcessingPhases = true;
	ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
	// 纯 fragment 数据修改，不触碰 ISM/世界对象，允许并行 chunk 执行
	// 执行组：渲染同步处理器按此组名排在其后（同帧读到最新角度）
	ExecutionOrder.ExecuteInGroup = TEXT("SatelliteOrbitMovement");
}

void USatelliteOrbitMovementProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassOrbitFragment>(EMassFragmentAccess::ReadWrite);
}

void USatelliteOrbitMovementProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const float DeltaTime = Context.GetDeltaTimeSeconds();

	EntityQuery.ForEachEntityChunk(EntityManager, Context, [DeltaTime](FMassExecutionContext& ChunkContext)
	{
		const TArrayView<FMassOrbitFragment> OrbitFragments = ChunkContext.GetMutableFragmentView<FMassOrbitFragment>();
		const int32 NumEntities = ChunkContext.GetNumEntities();

		for (int32 Index = 0; Index < NumEntities; ++Index)
		{
			UpdateOrbitMovement(OrbitFragments[Index], DeltaTime);
		}
	});
}
