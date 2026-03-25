// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/RotationController.h"

#include "Kismet/GameplayStatics.h"
#include "Planet/DayNightCycle.h"
#include "Planet/PlanetActor.h"


ARotationController::ARotationController()
{
	PrimaryActorTick.bCanEverTick = true;

	
}

void ARotationController::BeginPlay()
{
	Super::BeginPlay();

	// 延迟 0.1 秒获取引用，确保其他 Actor 已生成
	GetWorldTimerManager().SetTimer(
		InitTimerHandle,
		this,
		&ThisClass::InitializeReferences,
		0.1f,
		false
	);
}

void ARotationController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsRotating)
	{
		return;
	}
    
	// 计算角度增量
	float DeltaAngle = RotationSpeed * DeltaTime;
    
	// 累加角度
	CurrentAngle += DeltaAngle;
    
	// 角度归一化（0-360）
	if (CurrentAngle >= 360.0f)
	{
		CurrentAngle = FMath::Fmod(CurrentAngle, 360.0f);
	}
    
	// 更新旋转
	UpdateRotation();
}

void ARotationController::UpdateRotation()
{
	// 更新星球
	if (Planet && Planet->IsValidLowLevel())
	{
		IPlanetInterface::Execute_UpdateRotation(Planet, CurrentAngle);
	}
    
	// 更新昼夜系统
	if (DayNightCycle && DayNightCycle->IsValidLowLevel())
	{
		DayNightCycle->UpdateRotation(CurrentAngle);
	}
}

void ARotationController::InitializeReferences()
{
	// 获取星球
	TArray<AActor*> FoundPlanets;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlanetActor::StaticClass(), FoundPlanets);
	if (FoundPlanets.Num() > 0)
	{
		Planet = Cast<APlanetActor>(FoundPlanets[0]);
		UE_LOG(LogTemp, Log, TEXT("Planet found: %s"), *Planet->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("No Planet actor found in world!"));
	}
    
	// 获取昼夜系统
	TArray<AActor*> FoundDayNightCycles;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADayNightCycle::StaticClass(), FoundDayNightCycles);
	if (FoundDayNightCycles.Num() > 0)
	{
		DayNightCycle = Cast<ADayNightCycle>(FoundDayNightCycles[0]);
		UE_LOG(LogTemp, Log, TEXT("DayNightCycle found: %s"), *DayNightCycle->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("No DayNightCycle actor found in world!"));
	}
}

