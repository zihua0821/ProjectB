// Fill out your copyright notice in the Description page of Project Settings.


#include "Farm/FarmlandActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Farm/FarmlandComponent.h"


AFarmlandActor::AFarmlandActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;

	// RootSceneComponent
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	SetRootComponent(RootSceneComponent);
	
	// FarmlandComponent
	FarmlandComponent = CreateDefaultSubobject<UFarmlandComponent>(TEXT("FarmlandComponent"));
	FarmlandComponent->SetupAttachment(RootSceneComponent);

	// GroundMeshComponent
	GroundMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundMesh"));
	GroundMeshComponent->SetupAttachment(RootSceneComponent);
	GroundMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GroundMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);

}

void AFarmlandActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 初始化组件
	InitializeComponents();
}

void AFarmlandActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	check(FarmlandConfig);
	if (FarmlandComponent)
	{
		FarmlandComponent->SetFarmlandConfig(FarmlandConfig);
	}

	check(CropDataTable);
	if (FarmlandComponent)
	{
		FarmlandComponent->SetCropDataTable(CropDataTable);
	}
}

void AFarmlandActor::BeginPlay()
{
	Super::BeginPlay();
	
}

FVector AFarmlandActor::GetCellWorldLocation(int32 CellX, int32 CellY) const
{
	if (!FarmlandComponent) return GetActorLocation();
    
	// 获取组件相对位置
	FVector ComponentLocation = FarmlandComponent->GetComponentLocation();
    
	// 计算单元格在组件内的本地位置
	float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing;
	float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing;
    
	// 从中心开始计算（0,0在中心）
	float OffsetX = (CellX - (FarmlandConfig->GridSizeX - 1) * 0.5f) * (FarmlandConfig->CellSize.X + FarmlandConfig->CellSpacing);
	float OffsetY = (CellY - (FarmlandConfig->GridSizeY - 1) * 0.5f) * (FarmlandConfig->CellSize.Y + FarmlandConfig->CellSpacing);
    
	// 应用组件的旋转
	FRotator ComponentRotation = FarmlandComponent->GetComponentRotation();
	FVector LocalOffset(OffsetX, OffsetY, 0);
	FVector RotatedOffset = ComponentRotation.RotateVector(LocalOffset);
    
	return ComponentLocation + RotatedOffset;
}

bool AFarmlandActor::GetCellCoordinatesFromClick(const FVector& ClickLocation, FIntPoint& OutCellCoords) const
{
	if (!FarmlandComponent)
	{
		return false;
	}
    
	return FarmlandComponent->GetClickedCellCoordinates(ClickLocation, OutCellCoords);
}

FFarmLandPlotInfo AFarmlandActor::GetPlotInfo(const FIntPoint& PlotCoords) const
{
	if (!FarmlandComponent)
	{
		return FFarmLandPlotInfo();
	}
	return FarmlandComponent->GetPlotInfo(PlotCoords);
}

TArray<FFarmLandPlotSaveGameData> AFarmlandActor::GetPlotSaveGameData() const
{
	if (!FarmlandComponent)
	{
		return TArray<FFarmLandPlotSaveGameData>();
	}
	return FarmlandComponent->GetPlotSaveGameData();
}

void AFarmlandActor::SetPlotSaveGameData(const TArray<FFarmLandPlotSaveGameData>& InPlotSaveGameData)
{
	if (!FarmlandComponent)
	{
		return;
	}
	FarmlandComponent->SetPlotSaveGameData(InPlotSaveGameData);
}

void AFarmlandActor::HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint PlowCoords) const
{
	if (!FarmlandComponent)
	{
		return;
	}
	FarmlandComponent->HandleLeftMouseClicked(EditMode, PlowCoords);
}

void AFarmlandActor::PlantCrop(FIntPoint PlowCoords)
{
	if (!FarmlandComponent)
	{
		return;
	}
	FarmlandComponent->PlantCrop(PlowCoords);
}

bool AFarmlandActor::HarvestCrop(FIntPoint Coords)
{
	if (!FarmlandComponent)
	{
		return false;
	}
	return FarmlandComponent->HarvestCrop(Coords);
}

void AFarmlandActor::SetCropDry(FIntPoint Coords, bool bDry)
{
	if (!FarmlandComponent)
	{
		return;
	}
	if (bDry)
	{
		FarmlandComponent->CropDried(Coords);
	}
	else
	{
		FarmlandComponent->UnCropDried(Coords);
	}
}

void AFarmlandActor::OutlinePlot(const FIntPoint Coords, const bool bOutline) const
{
	if (!FarmlandComponent)
	{
		return;
	}
	if (bOutline)
	{
		FarmlandComponent->CreateOutline(Coords);
	}
	else
	{
		FarmlandComponent->RemoveOutline(Coords);
	}
}

void AFarmlandActor::InitializeComponents()
{
	ensure(FarmlandConfig);
	ensure(FarmlandConfig->GroundMesh);
	
	if (bComponentsInitialized && GetWorld()->WorldType != EWorldType::Editor)
	{
		return;
	}
	bComponentsInitialized = true;
	
	// 设置地面网格
	if (FarmlandConfig->bUseGround)
	{
		GroundMeshComponent->SetStaticMesh(FarmlandConfig->GroundMesh);
        
		// 计算并设置地面缩放
		UpdateGroundMeshSize();
	}
	else 
	{
		GroundMeshComponent->SetStaticMesh(nullptr);
	}
	// 设置地块组件位置
	FarmlandComponent->SetRelativeLocation(FVector(0.f, 0.f, FarmlandConfig->CellOffsetZ));
}

void AFarmlandActor::UpdateGroundMeshSize() const
{
	if (!GroundMeshComponent || !FarmlandConfig->GroundMesh) return;

	// 计算农场总大小
	FVector2D FarmSize = CalculateFarmTotalSize();
    
	// 获取地面网格的原始边界
	FBoxSphereBounds GroundBounds = FarmlandConfig->GroundMesh->GetBounds();
	FVector GroundExtent = GroundBounds.BoxExtent;
    
	if (GroundExtent.X > 0 && GroundExtent.Y > 0)
	{
		// 计算需要的缩放
		FVector NewScale;
		NewScale.X = FarmSize.X / (GroundExtent.X * 2.0f);
		NewScale.Y = FarmSize.Y / (GroundExtent.Y * 2.0f);
		NewScale.Z = 1.0f; // 保持原始高度
        
		GroundMeshComponent->SetRelativeScale3D(NewScale);
        
		// 确保地面在土地下方
		// GroundMeshComponent->SetRelativeLocation(FVector(0, 0, GroundExtent.Z));
	}
}

void AFarmlandActor::DrawDebugGrid() const
{
	 if (!GetWorld()) return;
    
    const FVector ActorLocation = GetActorLocation();
    const FVector2D FarmSize = CalculateFarmTotalSize();
    
    // 绘制农场边界
    const FColor BoundaryColor = FColor::Green;
    const float BoundaryHeight = 50.0f;
    const float LineThickness = 2.0f;
    
    // 计算边界四个角
    FVector Corners[4];
    Corners[0] = ActorLocation + FVector(-FarmSize.X * 0.5f, -FarmSize.Y * 0.5f, 0);
    Corners[1] = ActorLocation + FVector(FarmSize.X * 0.5f, -FarmSize.Y * 0.5f, 0);
    Corners[2] = ActorLocation + FVector(FarmSize.X * 0.5f, FarmSize.Y * 0.5f, 0);
    Corners[3] = ActorLocation + FVector(-FarmSize.X * 0.5f, FarmSize.Y * 0.5f, 0);
    
    // 绘制边界框
    for (int32 i = 0; i < 4; i++)
    {
        int32 Next = (i + 1) % 4;
        DrawDebugLine(GetWorld(), Corners[i], Corners[Next], BoundaryColor, false, FarmlandConfig->DebugDuration, 0, LineThickness);
    }
    
    // 绘制单元格网格
    if (FarmlandComponent)
    {
        const FColor GridColor = FColor::Cyan;
        const float GridHeight = 100.0f;
        
        // 绘制所有单元格
        for (int32 X = 0; X < FarmlandConfig->GridSizeX; X++)
        {
            for (int32 Y = 0; Y < FarmlandConfig->GridSizeY; Y++)
            {
                FVector CellCenter = GetCellWorldLocation(X, Y);
                FVector CellExtent = FVector(FarmlandConfig->CellSize.X * 0.5f, FarmlandConfig->CellSize.Y * 0.5f, GridHeight);
                
                // 绘制单元格边界框
                DrawDebugBox(GetWorld(), CellCenter, CellExtent, GridColor, false, FarmlandConfig->DebugDuration, 0, LineThickness);
                
                // 在单元格中心显示坐标
                FString CoordText = FString::Printf(TEXT("(%d,%d)"), X, Y);
                DrawDebugString(GetWorld(), CellCenter + FVector(0, 0, 20), CoordText, nullptr, FColor::White, FarmlandConfig->DebugDuration, true);
            }
        }
    }
    
    // 绘制信息文本
    FString InfoText = FString::Printf(TEXT("Farmland: %dx%d cells\nSpacing: %.0f"), 
        FarmlandConfig->GridSizeX, FarmlandConfig->GridSizeY, FarmlandConfig->CellSpacing);
    DrawDebugString(GetWorld(), ActorLocation + FVector(0, 0, 100), InfoText, nullptr, FColor::Yellow, FarmlandConfig->DebugDuration, true);
}

FVector2D AFarmlandActor::CalculateFarmTotalSize() const
{
	float Width = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
	float Height = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
	return FVector2D(Width, Height);
}

// Called every frame
void AFarmlandActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 调试绘制
	if (FarmlandConfig->bShowDebugGrid)
	{
		DrawDebugGrid();
	}

	
}

