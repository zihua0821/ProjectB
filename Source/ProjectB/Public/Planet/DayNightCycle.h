// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNightCycle.generated.h"

UCLASS()
class PROJECTB_API ADayNightCycle : public AActor
{
	GENERATED_BODY()

public:
	ADayNightCycle();

protected:
	virtual void BeginPlay() override;

public:
	// 根组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootSceneComponent;

	// 天空球网格组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SkySphere;

	// 太阳网格组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> SunSphere;
	
	// 天空光照组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkyLightComponent> SkyLight;

	// 太阳光源组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> SunLight;

	// 背光光源组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> BackLight;

	// 当前角度（0-360）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DayNight")
	float CurrentAngle = 90.f;
	
	// 接口实现：接收旋转更新
	virtual void UpdateRotation(float NewAngle);
	
};
