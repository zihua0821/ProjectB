// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/DayNightCycle.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Planet/PlanetTimeConfig.h"


ADayNightCycle::ADayNightCycle()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	
	// 创建根组件
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	SetRootComponent(RootSceneComponent);

	// 创建天空球体
	SkySphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkySphere"));
	SkySphere->SetupAttachment(RootSceneComponent);
	SkySphere->SetCastShadow(false);
	SkySphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkySphere->SetMobility(EComponentMobility::Movable);
	SkySphere->SetWorldScale3D(FVector(10000.0f));

	// 创建太阳球体
	SunSphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SunSphere"));
	SunSphere->SetupAttachment(RootSceneComponent);
	SunSphere->SetCastShadow(false);
	SunSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SunSphere->SetMobility(EComponentMobility::Movable);
	
	// 创建天空光照
	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(RootSceneComponent);
	SkyLight->SetIntensity(20.f);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SetCastShadows(false);
	SkyLight->SetLightColor(FLinearColor(0.05f, 0.05f, 0.1f));  

	// 创建太阳光源
	SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
	SunLight->SetupAttachment(RootSceneComponent);
	SunLight->SetIntensity(10.0f);
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetCastShadows(true);
	SunLight->SetLightColor(FLinearColor::White);  
	SunLight->SetAtmosphereSunLight(false);
	SunLight->SetForwardShadingPriority(0);

	// 创建背光光源
	BackLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("BackLight"));
	BackLight->SetupAttachment(RootSceneComponent);
	BackLight->SetIntensity(1.0f);
	BackLight->SetMobility(EComponentMobility::Movable);
	BackLight->SetCastShadows(false);
	BackLight->SetLightColor(FLinearColor::White);  
	BackLight->SetAtmosphereSunLight(false);
	BackLight->SetForwardShadingPriority(1);
}

void ADayNightCycle::BeginPlay()
{
	Super::BeginPlay();

	// 从配置加载初始值
	if (TimeConfig)
	{
		CurrentTimeOfDay = TimeConfig->InitialTimeOfDay;
		CurrentSeasonProgress = TimeConfig->InitialSeasonProgress;
		CurrentTimeSpeedMultiplier = TimeConfig->TimeSpeedMultiplier;
	}

	OnSeasonChanged.Broadcast(LastSeasonIndex);
}

void ADayNightCycle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (TimeConfig == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("ADayNightCycle::Tick - TimeConfig is null!"));
		return;
	}

	// 如果时间暂停，跳过更新
	if (CurrentTimeSpeedMultiplier <= 0.0f)
	{
		return;
	}

	// 1. 累加时间
	// 累加后立即取模，保持数值较小
	float TotalCycle = TimeConfig->DayDurationSeconds + TimeConfig->NightDurationSeconds;
	float TotalYearCycle = TimeConfig->SeasonDurationSeconds * 4.0f;
	float MinCycle = FMath::Min(TotalCycle, TotalYearCycle);

	TimeAccumulator += DeltaSeconds * TimeConfig->TimeSpeedMultiplier;
	if (TimeAccumulator >= MinCycle * 100.0f)  // 保留 100 个周期的余量
	{
		TimeAccumulator = FMath::Fmod(TimeAccumulator, MinCycle);
	}

	// 2. 计算日夜循环
	float TotalDayDuration = TimeConfig->DayDurationSeconds + TimeConfig->NightDurationSeconds;
	float DayProgress = FMath::Fmod(TimeAccumulator, TotalDayDuration) / TotalDayDuration;
	CurrentTimeOfDay = TimeConfig->InitialTimeOfDay + DayProgress * 24.0f;
	CurrentTimeOfDay = FMath::Fmod(CurrentTimeOfDay, 24.0f);
	if (CurrentTimeOfDay < 0.0f)
	{
		CurrentTimeOfDay += 24.0f;
	}

	// 3. 计算四季循环
	if (TimeConfig->bEnableSeasons)
	{
		float TotalYearDuration = TimeConfig->SeasonDurationSeconds * 4.0f;
		float YearProgress = TimeAccumulator / TotalYearDuration;
		CurrentSeasonProgress = TimeConfig->InitialSeasonProgress + YearProgress;
		CurrentSeasonProgress = FMath::Fmod(CurrentSeasonProgress, 1.0f);
		if (CurrentSeasonProgress < 0.0f)
		{
			CurrentSeasonProgress += 1.0f;
		}
	}
	else
	{
		CurrentSeasonProgress = 0.0f;
	}

	// 4. 更新旋转
	UpdateRotationFromTime(CurrentTimeOfDay, CurrentSeasonProgress);

	UpdateEvent();
}

void ADayNightCycle::UpdateRotation(FRotator NewRotation)
{
	SetActorRotation(NewRotation);
}

void ADayNightCycle::UpdateEvent()
{
	// 触发时间改变事件
	OnTimeChanged.Broadcast(CurrentTimeOfDay);
	
	// 计算当前季节索引 (0=春，1=夏，2=秋，3=冬)
	int32 CurrentSeasonIndex = FMath::FloorToInt(CurrentSeasonProgress * 4.0f);
	if (CurrentSeasonIndex != LastSeasonIndex)
	{
		OnSeasonChanged.Broadcast(CurrentSeasonIndex);
		LastSeasonIndex = CurrentSeasonIndex;
	}
}

void ADayNightCycle::UpdateRotationFromTime(float TimeOfDay, float SeasonProgress)
{
	// 计算偏航角 (Yaw) - 太阳东升西落
	// 12 点时太阳在正南 (Yaw=0)，6 点在东 (Yaw=-90)，18 点在西 (Yaw=90)
	float YawAngle = (TimeOfDay - 12.0f) * 15.0f;

	// 计算俯仰角 (Pitch) - 季节影响太阳高度
	// 夏季 (0.25) 太阳最高，冬季 (0.75) 太阳最低
	float TiltAngle = 0.0f;
	if (TimeConfig && TimeConfig->bEnableSeasons)
	{
		TiltAngle = TimeConfig->AxialTilt * FMath::Sin(2.0f * PI * (SeasonProgress - 0.25f));
	}
    
	// 设置旋转 (UE 坐标系：Z 轴向上)
	FRotator NewRotation = FRotator(TiltAngle, YawAngle, 0.0f);
	UpdateRotation(NewRotation);

}

void ADayNightCycle::SetTimeOfDay(float NewTime)
{
	CurrentTimeOfDay = FMath::Fmod(NewTime, 24.0f);
	if (CurrentTimeOfDay < 0.0f)
	{
		CurrentTimeOfDay += 24.0f;
	}
	UpdateRotationFromTime(CurrentTimeOfDay, CurrentSeasonProgress);
}

void ADayNightCycle::SetSeasonProgress(float NewProgress)
{
	CurrentSeasonProgress = FMath::Clamp(NewProgress, 0.0f, 1.0f);
	UpdateRotationFromTime(CurrentTimeOfDay, CurrentSeasonProgress);
}

void ADayNightCycle::SetTimeSpeedMultiplier(float Multiplier)
{
	CurrentTimeSpeedMultiplier = Multiplier;
}




