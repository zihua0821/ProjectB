// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OrbitManagerComponent.h"
#include "GameFramework/MovementComponent.h"
#include "OrbitMovementComponent.generated.h"

// 移动状态枚举
UENUM(BlueprintType)
enum class ESatelliteMoveState : uint8
{
	Idle             UMETA(DisplayName = "闲置"),
	MovingToTarget   UMETA(DisplayName = "移动到目标"),
	Paused           UMETA(DisplayName = "暂停")
};

DECLARE_MULTICAST_DELEGATE(FOnMoveToTargetCompleted);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTB_API UOrbitMovementComponent : public UMovementComponent
{
	GENERATED_BODY()

public:
	UOrbitMovementComponent();
	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;
	// 轨道配置 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	FOrbitData OrbitData;

	// 角速度 (度/秒)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	float AngularVelocity = 10.0f;

	// 运行方向：true=顺时针, false=逆时针
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	bool bClockwise = false;

	// 获取当前相位角
	UFUNCTION(BlueprintCallable)
	float GetCurrentAngle() const { return CurrentAngle; }
	
	// 移动状态
	FOnMoveToTargetCompleted OnMoveToTargetCompleted;

	// 设置移动状态
	UFUNCTION(BlueprintCallable)
	void SetMoveState(const ESatelliteMoveState NewState) { MoveState = NewState;}

	// 获取移动状态
	UFUNCTION(BlueprintCallable)
	ESatelliteMoveState GetMoveState() const { return MoveState; }

	// 移动到目标角度
	UFUNCTION(BlueprintCallable)
	void StartMoveToTargetAngle(const float NewTargetAngle)
	{
		TargetAngle = FMath::Fmod(NewTargetAngle, 360.0f);
		if (TargetAngle < 0) TargetAngle += 360.0f;
		MoveState = ESatelliteMoveState::MovingToTarget;
	}

	// 暂停移动
	UFUNCTION(BlueprintCallable)
	void PauseMovement()
	{
		if (MoveState != ESatelliteMoveState::Paused) MoveState = ESatelliteMoveState::Paused;
	}

	// 恢复移动
	UFUNCTION(BlueprintCallable)
	void ResumeMovement()
	{
		if (MoveState == ESatelliteMoveState::Paused) MoveState = ESatelliteMoveState::Idle;
	}

protected:
	virtual void BeginPlay() override;

private:
	// 更新轨道移动
	void UpdateOrbitMovement(float DeltaTime);

	// 计算轨道位置
	FVector CalculateOrbitPosition(float Angle) const;

	// 移动状态
	ESatelliteMoveState MoveState = ESatelliteMoveState::Idle;

	// 目标角度
	float TargetAngle = 0.0f;

	// 当前相位角 (度)
	float CurrentAngle = 0.0f;
};
