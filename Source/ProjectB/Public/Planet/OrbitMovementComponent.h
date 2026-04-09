// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OrbitManagerComponent.h"
#include "GameFramework/MovementComponent.h"
#include "OrbitMovementComponent.generated.h"


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

	// 当前相位角 (度)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orbit")
	float CurrentAngle = 0.0f;

protected:
	virtual void BeginPlay() override;

private:
	void UpdateOrbitMovement(float DeltaTime);

};
