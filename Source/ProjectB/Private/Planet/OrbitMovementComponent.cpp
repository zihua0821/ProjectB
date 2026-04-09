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

	// 1. 计算在轨道平面上的初始位置
	const float RadAngle = FMath::DegreesToRadians(CurrentAngle);
	const FVector OrbitPlanePosition = FVector(
		OrbitData.Radius * FMath::Cos(RadAngle),
		OrbitData.Radius * FMath::Sin(RadAngle),
		0.0f
	);

	// 2. 转换到世界坐标
	FVector StartLocation = OrbitData.Transform.TransformPosition(OrbitPlanePosition);

	// 3. 强制设置位置 (忽略碰撞，确保准确出生)
	UpdatedComponent->SetWorldLocation(StartLocation);
}


void UOrbitMovementComponent::UpdateOrbitMovement(float DeltaTime)
{
	if (!UpdatedComponent || FMath::IsNearlyZero(AngularVelocity) || FMath::IsNearlyZero(OrbitData.Radius))
	{
		return;
	}

	// 1. 根据方向更新角度
	if (bClockwise)
	{
		CurrentAngle -= AngularVelocity * DeltaTime;
	}
	else
	{
		CurrentAngle += AngularVelocity * DeltaTime;
	}

	// 角度归一化 (保持数值在合理范围)
	if (CurrentAngle >= 360.0f) CurrentAngle -= 360.0f;
	if (CurrentAngle <= -360.0f) CurrentAngle += 360.0f;

	// 2. 计算在轨道平面上的位置
	// 轨道平面：XY平面是轨道平面，Z轴是轨道法线
	const float RadAngle = FMath::DegreesToRadians(CurrentAngle);
	const FVector OrbitPlanePosition = FVector(
		OrbitData.Radius * FMath::Cos(RadAngle),
		OrbitData.Radius * FMath::Sin(RadAngle),
		0.0f
	);

	// 3. 转换到世界空间
	// Transform定义了：
	// - Location: 轨道中心（星球中心）
	// - Rotation: 轨道平面的方向
	//   - Roll (X): 轨道方位角（绕X轴旋转）
	//   - Pitch (Y): 轨道倾角（绕Y轴旋转）
	//   - Yaw (Z): 相位零点（绕Z轴旋转）
	const FVector WorldLocation = OrbitData.Transform.TransformPosition(OrbitPlanePosition);

	// 4. 执行移动
	MoveUpdatedComponent(WorldLocation - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true);
}

void UOrbitMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
	UpdateOrbitMovement(DeltaTime);
}

