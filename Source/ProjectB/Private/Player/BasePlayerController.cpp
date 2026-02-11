// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectB/Public/Player/BasePlayerController.h"

#include "Crops/CropBase.h"
#include "Farm/FarmlandActor.h"
#include "ProjectB/Public/TimeSync/TimeSyncComponent.h"

ABasePlayerController::ABasePlayerController()
{
	TimeSyncComponent = CreateDefaultSubobject<UTimeSyncComponent>(TEXT("TimeSyncComponent"));
}

void ABasePlayerController::BeginPlay()
{
	Super::BeginPlay();

	SetShowMouseCursor(true);
}

void ABasePlayerController::OnLeftMouseClick()
{
	// 执行射线检测
	FVector HitLocation;
	AFarmlandActor* HitFarmland = nullptr;
	FIntPoint CellCoords;
	if (TraceForFarmlandAndCell(HitLocation, HitFarmland, CellCoords))
	{
		// DrawDebugSphere(GetWorld(), HitLocation, 10.f, 12, FColor::Red, false, 3.f);
		const FFarmLandPlotInfo& PlotInfo = HitFarmland->GetPlotInfo(CellCoords);
		// HitFarmland->HandleLeftMouseClicked(EditMode, CellCoords);
		if (PlotInfo.Crop)
		{
			// HitFarmland->HarvestCrop(CellCoords);
			if (!PlotInfo.Crop->GetCropData().CropStateData.HasState(ECropState::NeedsWater))
			{
				HitFarmland->SetCropDry(CellCoords,  true);
			}
			else if (PlotInfo.Crop->GetCropData().CropStateData.HasState(ECropState::NeedsWater))
			{
				HitFarmland->SetCropDry(CellCoords, false);
			}
			
		}
		else
		{
			HitFarmland->PlantCrop(CellCoords, "Corn");
		}
		
		// GEngine->AddOnScreenDebugMessage(-1,
		// 		3.f,
		// 		FColor::Red,
		// 		FString::Printf(TEXT("Hit Farmland: %s (%d,%d) %s"), *HitFarmland->GetName(), CellCoords.X, CellCoords.Y, *PlotInfo.StateData.ToString()));
	}
}

bool ABasePlayerController::TraceForFarmlandAndCell(FVector& OutHitLocation, AFarmlandActor*& OutFarmland,
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
            FCollisionQueryParams QueryParams;
            QueryParams.bTraceComplex = true;
            QueryParams.bReturnPhysicalMaterial = false;
            
            // 忽略玩家自身
            if (GetPawn())
            {
                QueryParams.AddIgnoredActor(GetPawn());
            }
            
            // 执行射线检测
            if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
            {
                OutHitLocation = HitResult.Location;
                
                // 检查是否击中了FarmlandActor或其子组件
                AActor* HitActor = HitResult.GetActor();
                if (HitActor)
                {
                    // 检查是否是FarmlandActor
                    OutFarmland = Cast<AFarmlandActor>(HitActor);
                    
                    // 如果不是直接击中FarmlandActor，检查是否是它的组件
                    if (!OutFarmland)
                    {
                        OutFarmland = Cast<AFarmlandActor>(HitActor->GetOwner());
                    }
                    
                    // 如果找到了FarmlandActor，计算单元格坐标
                    if (OutFarmland && OutFarmland->GetFarmlandComponent())
                    {
                        return OutFarmland->GetCellCoordinatesFromClick(OutHitLocation, OutCellCoords);
                    }
                }
            }
        }
    }
    
    return false;
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