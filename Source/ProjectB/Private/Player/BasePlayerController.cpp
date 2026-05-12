// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectB/Public/Player/BasePlayerController.h"

#include "Crops/CropBase.h"
#include "Farm/CellManagerComponent.h"
#include "Farm/FarmlandActor.h"
#include "Farm/FarmlandComponent.h"
#include "Planet/PlanetActor.h"
#include "ProjectB/Public/TimeSync/TimeSyncComponent.h"

ABasePlayerController::ABasePlayerController()
{
	TimeSyncComponent = CreateDefaultSubobject<UTimeSyncComponent>(TEXT("TimeSyncComponent"));
}

void ABasePlayerController::SetEditMode(const EEditMode InEditMode)
{
	EditMode = InEditMode;
	OnEditModeChanged.Broadcast(EditMode);
}

void ABasePlayerController::BeginPlay()
{
	Super::BeginPlay();

	SetShowMouseCursor(true);
	FInputModeGameAndUI InputMode;
	// 关键：必须设为 true，否则点击 UI 仍会穿透到场景
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	
	// 获取GameInstance并尝试加载存档
	GameInstance = Cast<UProjectBGameInstance>(GetGameInstance());
	if (GameInstance)
	{
		GameInstance->LoadGame();
	}
}

void ABasePlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// double StartTime = FPlatformTime::Seconds();
	UpdateHoveredPlot();
	// UE_LOG(LogTemp, Warning, TEXT("UpdateHoveredPlot took: %.3f ms"), (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void ABasePlayerController::OnLeftMouseClick()
{
	// 执行射线检测
	FVector HitLocation;
	APlanetActor* HitPlanet = nullptr;
	FIntPoint CellCoords;
	if (TraceForFarmlandAndCell(HitLocation, HitPlanet, CellCoords))
	{
		// DrawDebugSphere(GetWorld(), HitLocation, 10.f, 12, FColor::Red, false, 3.f);
		// const FFarmLandPlotInfo& PlotInfo = HitFarmland->GetPlotInfo(CellCoords);
		
		HitPlanet->HandleLeftMouseClicked(EditMode, CellCoords);
		
		// GEngine->AddOnScreenDebugMessage(-1,
		// 		3.f,
		// 		FColor::Red,
		// 		FString::Printf(TEXT("Hit :(%d,%d)"), CellCoords.X, CellCoords.Y));
	}
}

bool ABasePlayerController::TraceForFarmlandAndCell(FVector& OutHitLocation, APlanetActor*& OutPlanetActor,
	FIntPoint& OutCellCoords)
{
	// 获取鼠标位置
    float MouseX, MouseY;
    if (GetMousePosition(MouseX, MouseY))
    {
        // 将鼠标位置转换为世界空间射线
        FVector WorldLocation, WorldDirection;
        if (DeprojectScreenPositionToWorld(MouseX, MouseY, WorldLocation, WorldDirection))
        {
            // 计算射线终点
            float TraceDistance = 10000.0f; // 10米
            FVector TraceStart = WorldLocation;
            FVector TraceEnd = TraceStart + WorldDirection * TraceDistance;
            
            // 执行射线检测
            FHitResult HitResult;
        	
        	// 指定只检测 WorldDynamic 类型
        	FCollisionObjectQueryParams ObjectQueryParams;
        	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
 
        	// 设置参数
        	FCollisionQueryParams QueryParams;
        	QueryParams.bTraceComplex = false;
        	QueryParams.bReturnPhysicalMaterial = false;
        	// 忽略玩家自身
        	if (GetPawn())
        	{
        		QueryParams.AddIgnoredActor(GetPawn());
        	}
        
            if (GetWorld()->LineTraceSingleByObjectType(HitResult, TraceStart, TraceEnd, ObjectQueryParams, QueryParams))
            {
                OutHitLocation = HitResult.Location;
                // 检查是否击中了FarmlandActor或其子组件
                AActor* HitActor = HitResult.GetActor();
                if (HitActor)
                {
                    // 检查是否是PlanetActor
                    OutPlanetActor = Cast<APlanetActor>(HitActor);
                    
                    // 如果不是直接击中PlanetActor，检查是否是它的组件
                    if (!OutPlanetActor)
                    {
                        OutPlanetActor = Cast<APlanetActor>(HitActor->GetOwner());
                    }
                    
                    // 如果找到了FarmlandActor，计算单元格坐标
                    if (OutPlanetActor)
                    {
                    	if (const UCellManagerComponent* CellManagerComponent = OutPlanetActor->GetCellManagerComponent())
                    	{
                    		float HitDistance;
                    		if (CellManagerComponent->GetCellCoordsFromScreenMouse(OutCellCoords, HitDistance))
                    		{
                    			return true;
                    		}
                    	}
                    }
                }
            }
        }
    }
    
    return false;
}

void ABasePlayerController::UpdateHoveredPlot()
{
	// 执行射线检测
	FVector HitLocation;
	APlanetActor* HitPlanet = nullptr;
	FIntPoint CellCoords = FIntPoint(-1, -1);

	if (TraceForFarmlandAndCell(HitLocation, HitPlanet, CellCoords))
	{
		// DrawDebugSphere(GetWorld(), HitLocation, 5.f, 12, FColor::Red, false, 3.f);
		if (PreHoveredPlotCoords == CellCoords)
		{
			return;
		}
		OnHoveredPlot.Broadcast(HitPlanet, CellCoords);
		if (PreHoveredPlanetActor && PreHoveredPlanetActor != HitPlanet)
		{
			PreHoveredPlanetActor->OutlinePlot(PreHoveredPlotCoords, false);
		}
		else
		{
			HitPlanet->OutlinePlot(PreHoveredPlotCoords, false);
		}
		HitPlanet->OutlinePlot(CellCoords, true);
		PreHoveredPlotCoords = CellCoords;
		PreHoveredPlanetActor = HitPlanet;
	}
	else
	{
		OnHoveredPlot.Broadcast(nullptr, FIntPoint(-1, -1));
		if (PreHoveredPlanetActor)
		{
			PreHoveredPlanetActor->OutlinePlot(PreHoveredPlotCoords, false);
			PreHoveredPlotCoords = FIntPoint(-1, -1);
			PreHoveredPlanetActor = nullptr;
		}
	}
}

// 切换到下一个编辑模式（正向循环）
void ABasePlayerController::SwitchToNextEditMode()
{
	constexpr int32 EditModeCount = static_cast<int32>(EEditMode::Count);
	int32 Index = static_cast<int32>(EditMode);
	Index = (Index + 1) % EditModeCount;
	EditMode = static_cast<EEditMode>(Index);
}

// 切换到上一个编辑模式（反向循环）
void ABasePlayerController::SwitchToPrevEditMode()
{
	constexpr int32 EditModeCount = static_cast<int32>(EEditMode::Count);
	int32 Index = static_cast<int32>(EditMode);
	Index = (Index - 1 + EditModeCount) % EditModeCount; // 避免负数
	EditMode = static_cast<EEditMode>(Index);
}