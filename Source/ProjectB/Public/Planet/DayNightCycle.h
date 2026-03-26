// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNightCycle.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTimeChanged, float, TimeOfDay);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSeasonChanged, float, SeasonProgress);

class UPlanetTimeConfig;

UCLASS()
class PROJECTB_API ADayNightCycle : public AActor
{
	GENERATED_BODY()

public:
	ADayNightCycle();

protected:
	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;
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

	// === 事件 ===

	// 时间改变事件
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnTimeChanged OnTimeChanged;

	// 季节改变事件
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnSeasonChanged OnSeasonChanged;

	// === 时间配置 ===
	
	// 时间配置 DataAsset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
	TObjectPtr<UPlanetTimeConfig> TimeConfig;

	// === 时间状态 ===

	// 当前时间
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Time State")
	float CurrentTimeOfDay = 12.0f;

	// 当前季节进度
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Time State")
	float CurrentSeasonProgress = 0.0f;

	// 当前时间流速倍数
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Time State")
	float CurrentTimeSpeedMultiplier = 1.f;

	// 时间累计
	float TimeAccumulator = 0.0f;

	// 上一季节索引
	int32 LastSeasonIndex = 0;

	// === 旋转接口 ===
	
	// 更新旋转
	virtual void UpdateRotation(FRotator NewRotation);

	// 使用时间更新旋转
	virtual void UpdateRotationFromTime(float TimeOfDay, float SeasonProgress);

	// === 控制接口 ===

	UFUNCTION(BlueprintPure, Category = "Time State")
	float GetTimeOfDay() const { return CurrentTimeOfDay; }

	UFUNCTION(BlueprintPure, Category = "Time State")
	float GetSeasonProgress() const { return CurrentSeasonProgress; }

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	void SetTimeOfDay(float NewTime);

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	void SetSeasonProgress(float NewProgress);

	UFUNCTION(BlueprintCallable, Category = "Time Control")
	void SetTimeSpeedMultiplier(float Multiplier);

private:
	FTimerHandle EventUpdateTimerHandle;
	
	void UpdateEvent();

};


