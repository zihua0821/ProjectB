// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RotationController.generated.h"

class ADayNightCycle;
class APlanetActor;

UCLASS()
class PROJECTB_API ARotationController : public AActor
{
	GENERATED_BODY()

public:
	ARotationController();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// 旋转速度（度/秒），默认 6（360 度/1 分钟）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation")
	float RotationSpeed = 6.f;

	// 当前角度（0-360）
	UPROPERTY(VisibleAnywhere, Category = "Rotation")
	float CurrentAngle = 0.f;

	// 是否正在旋转（调试开关）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rotation")
	bool bIsRotating = true;

	// 星球引用
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "References")
	TObjectPtr<APlanetActor> Planet;

	// 昼夜系统引用
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "References")
	TObjectPtr<ADayNightCycle> DayNightCycle;

	// 更新旋转（调用星球和昼夜系统）
	UFUNCTION(BlueprintCallable, Category = "Rotation")
	void UpdateRotation();

	// 初始化引用（延迟调用）
	UFUNCTION()
	void InitializeReferences();

private:
	// 获取引用计时器句柄
	FTimerHandle InitTimerHandle;
};
