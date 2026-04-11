// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "OrbitManagerComponent.h"
#include "GameFramework/Pawn.h"
#include "AutomationSatellite.generated.h"

class UOrbitMovementComponent;

UCLASS()
class PROJECTB_API AAutomationSatellite : public APawn
{
	GENERATED_BODY()

public:
	AAutomationSatellite();

	// 轨道运动组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UOrbitMovementComponent> OrbitMovementComponent;

	// 卫星网格体
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SatelliteMeshComponent;

	// 轨道配置 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ExposeOnSpawn = "true"))
	FOrbitData OrbitData;

	// 角速度 (度/秒)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ExposeOnSpawn = "true"))
	float AngularVelocity = 10.0f;

	// 运行方向：true=顺时针, false=逆时针
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ExposeOnSpawn = "true"))
	bool bClockwise = false;

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
};
