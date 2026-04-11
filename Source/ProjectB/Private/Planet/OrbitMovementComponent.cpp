// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/OrbitMovementComponent.h"


UOrbitMovementComponent::UOrbitMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


void UOrbitMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	if (!UpdatedComponent) return;

	// 计算起始位置
	const FVector StartLocation = CalculateOrbitPosition(CurrentAngle);

	// 强制设置位置 (忽略碰撞，确保准确出生)
	UpdatedComponent->SetWorldLocation(StartLocation);
}


void UOrbitMovementComponent::UpdateOrbitMovement(float DeltaTime)
{
	if (!UpdatedComponent || FMath::IsNearlyZero(AngularVelocity) || FMath::IsNearlyZero(OrbitData.Radius))
	{
		return;
	}

	float NewAngle = CurrentAngle;
	const float DeltaAngle = AngularVelocity * DeltaTime;

	switch (MoveState)
	{
	case ESatelliteMoveState::Idle:
		NewAngle += (bClockwise ? -DeltaAngle : DeltaAngle);
		break;

	case ESatelliteMoveState::MovingToTarget:
		{
			// 计算最短弧方向
			float Delta = TargetAngle - CurrentAngle;
			// 归一化到 [-180, 180]
			Delta = FMath::Fmod(Delta + 180.0f, 360.0f);
			if (Delta < 0) Delta += 360.0f;
			Delta -= 180.0f;

			if (FMath::Abs(Delta) <= 0.1f)
			{
				// 到达目标
				NewAngle = TargetAngle;
				MoveState = ESatelliteMoveState::Paused;  
				OnMoveToTargetCompleted.Broadcast();
			}
			else
			{
				// 向目标方向移动一步，不越过目标
				float Step = FMath::Min(FMath::Abs(Delta), DeltaAngle);
				NewAngle += (Delta > 0 ? Step : -Step);
			}
		}
		break;

	case ESatelliteMoveState::Paused:
		return; // 不移动

	default:
		break;
	}

	// 归一化角度到 [0, 360)
	NewAngle = FMath::Fmod(NewAngle, 360.0f);
	if (NewAngle < 0) NewAngle += 360.0f;
	CurrentAngle = NewAngle;

	// 更新位置
	const FVector WorldLocation = CalculateOrbitPosition(CurrentAngle);
	const FVector DeltaMove = WorldLocation - UpdatedComponent->GetComponentLocation();
	if (!DeltaMove.IsNearlyZero())
	{
		MoveUpdatedComponent(DeltaMove, UpdatedComponent->GetComponentQuat(), true);
	}
}

FVector UOrbitMovementComponent::CalculateOrbitPosition(const float Angle) const
{
	// 角度转换为弧度
	const float RadAngle = FMath::DegreesToRadians(Angle);

	// 计算在轨道平面的相对位置
	const FVector OrbitPlanePosition = FVector(
		OrbitData.Radius * FMath::Cos(RadAngle),
		OrbitData.Radius * FMath::Sin(RadAngle),
		0.0f
	);

	// 转换到世界空间
	return OrbitData.Transform.TransformPosition(OrbitPlanePosition);
}

void UOrbitMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
	UpdateOrbitMovement(DeltaTime);
}

