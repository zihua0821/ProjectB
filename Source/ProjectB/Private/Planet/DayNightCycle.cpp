// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/DayNightCycle.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"


ADayNightCycle::ADayNightCycle()
{
	PrimaryActorTick.bCanEverTick = false;

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

	// 初始化
	UpdateRotation(CurrentAngle);
}

void ADayNightCycle::UpdateRotation(float NewAngle)
{
	CurrentAngle = NewAngle;

	// 旋转天空盒和光源（绕 Y 轴）
	SetActorRotation(FRotator(0.0f, NewAngle, 0.0f));
	
}





