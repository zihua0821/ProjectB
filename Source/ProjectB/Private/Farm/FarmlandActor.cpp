// Fill out your copyright notice in the Description page of Project Settings.


#include "Farm/FarmlandActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Farm/FarmlandComponent.h"


// Sets default values
AFarmlandActor::AFarmlandActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
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

	LandCellRenderer = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LandCellRenderer"));
	LandCellRenderer->SetupAttachment(RootSceneComponent);
	LandCellRenderer->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	LandCellRenderer->SetCollisionResponseToAllChannels(ECR_Block);

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
		FarmlandComponent->SetPlotInfoIndex(PlotInfoIndex);
	}
}

void AFarmlandActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// #if WITH_EDITOR
// void AFarmlandActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
// {
// 	Super::PostEditChangeProperty(PropertyChangedEvent);
//
// 	// 检查是否是网格相关属性发生了变化
// 	FName PropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
//     
// 	// 定义需要监听的属性名
// 	static TArray<FName> GridProperties = {
// 		GET_MEMBER_NAME_CHECKED(AFarmlandActor, LandCellMesh),
// 		GET_MEMBER_NAME_CHECKED(AFarmlandActor, GroundMesh),
// 		GET_MEMBER_NAME_CHECKED(AFarmlandActor, CellSize),
// 		GET_MEMBER_NAME_CHECKED(AFarmlandActor, GridSizeX),
// 		GET_MEMBER_NAME_CHECKED(AFarmlandActor, GridSizeY),
// 		GET_MEMBER_NAME_CHECKED(AFarmlandActor, CellSpacing),
// 		GET_MEMBER_NAME_CHECKED(AFarmlandActor, LandHeightOffset),
// 		GET_MEMBER_NAME_CHECKED(AFarmlandActor, bShowDebugGrid),
// 		GET_MEMBER_NAME_CHECKED(AFarmlandActor, bAutoUpdateConfig)
// 	};
//
// 	if (GridProperties.Contains(PropertyName))
// 	{
// 		if (bAutoUpdateConfig)
// 		{
// 			// 延迟更新，避免在属性编辑过程中频繁重建
// 			FTimerHandle TimerHandle;
// 			GetWorld()->GetTimerManager().SetTimer(TimerHandle, [this]()
// 			{
// 				UpdateFarmlandConfiguration();
// 			}, 0.1f, false);
// 		}
// 	}
// }
//
// void AFarmlandActor::PostEditMove(bool bFinished)
// {
// 	Super::PostEditMove(bFinished);
//
// 	// 移动后重新计算单元格位置
// 	if (bFinished && bAutoUpdateConfig)
// 	{
// 		UpdateFarmlandConfiguration();
// 	}
// }
// #endif

FBox AFarmlandActor::GetFarmBounds() const
{
	if (!FarmlandComponent) return FBox(EForceInit::ForceInit);
    
	FVector2D FarmSize = CalculateFarmTotalSize();
	FVector Origin = GetActorLocation();
    
	// 计算边界框
	FVector Min = Origin - FVector(FarmSize.X * 0.5f, FarmSize.Y * 0.5f, 0);
	FVector Max = Origin + FVector(FarmSize.X * 0.5f, FarmSize.Y * 0.5f, 100.0f); // Z方向100单位高度
    
	return FBox(Min, Max);
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

bool AFarmlandActor::AreCellCoordinatesValid(int32 CellX, int32 CellY) const
{
	if (!FarmlandComponent)
	{
		return false;
	}
    
	return CellX >= 0 && CellX < FarmlandConfig->GridSizeX && 
		   CellY >= 0 && CellY < FarmlandConfig->GridSizeY;
}

FVector AFarmlandActor::GetCellCenterFromClick(const FVector& ClickLocation) const
{
	FIntPoint CellCoords;
	if (GetCellCoordinatesFromClick(ClickLocation, CellCoords))
	{
		return FarmlandComponent->CellToWorldCoordinates(CellCoords.X, CellCoords.Y);
	}
    
	return ClickLocation; // 返回原始点击位置作为回退
}

FFarmLandPlotInfo AFarmlandActor::GetPlotInfo(const FIntPoint& PlotCoords) const
{
	if (!FarmlandComponent)
	{
		return FFarmLandPlotInfo();
	}
	return FarmlandComponent->GetPlotInfo(PlotCoords);
}

void AFarmlandActor::PlowLand(const FIntPoint PlowCoords) const
{
	if (!FarmlandComponent)
	{
		return ;
	}
	FarmlandComponent->PlowLand(PlowCoords);
}

void AFarmlandActor::InitializeComponents()
{
	if (bComponentsInitialized && GetWorld()->WorldType != EWorldType::Editor)
	{
		return;
	}
	bComponentsInitialized = true;
	// 设置地面网格
	if (FarmlandConfig->GroundMesh)
	{
		GroundMeshComponent->SetStaticMesh(FarmlandConfig->GroundMesh);
        
		// 计算并设置地面缩放
		UpdateGroundMeshSize();
	}

	ensure(FarmlandConfig->LandCellMesh);
	if (FarmlandConfig->LandCellMesh)
	{
		LandCellRenderer->SetStaticMesh(FarmlandConfig->LandCellMesh);
	}
	// 清除所有现有实例
	LandCellRenderer->ClearInstances();
	PlotToLandInstanceIndex.Empty();
	PlotInfoIndex.Empty();
	
	// 计算缩放因子
	// 获取土地网格的原始边界
	FBoxSphereBounds CellBounds = FarmlandConfig->LandCellMesh->GetBounds();
	FVector CellExtent = CellBounds.BoxExtent;
	// 计算需要的缩放
	float ScaleFactorX = FarmlandConfig->CellSize.X / (CellExtent.X * 2.f);
	float ScaleFactorY = FarmlandConfig->CellSize.Y / (CellExtent.Y * 2.f);
	float ScaleFactorZ = FarmlandConfig->LandHeightOffset / (CellExtent.Z * 2.f);
	FVector InstanceScale(ScaleFactorX, ScaleFactorY, ScaleFactorZ);
    
	// 计算总偏移（从中心开始）
	float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing;
	float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing;
	float StartX = -TotalWidth * 0.5f + FarmlandConfig->CellSize.X * 0.5f;
	float StartY = -TotalHeight * 0.5f + FarmlandConfig->CellSize.Y * 0.5f;
    
	// 创建所有土地单元格实例
	for (int32 Y = 0; Y < FarmlandConfig->GridSizeY; Y++)
	{
		for (int32 X = 0; X < FarmlandConfig->GridSizeX; X++)
		{
			int32 PlotIndex = GetPlotIndex(X, Y);
            
			// 计算位置
			float PosX = StartX + X * (FarmlandConfig->CellSize.X + FarmlandConfig->CellSpacing);
			float PosY = StartY + Y * (FarmlandConfig->CellSize.Y + FarmlandConfig->CellSpacing);
			float PosZ = FarmlandConfig->LandHeightOffset / 2.0f;
            
			// 创建变换
			FTransform CellTransform;
			CellTransform.SetLocation(FVector(PosX, PosY, PosZ));
			CellTransform.SetScale3D(InstanceScale);
			FFarmLandPlotInfo PlotInfo;
			PlotInfo.Transform = CellTransform;
			// 添加到渲染器
			if (FarmlandConfig->bInitAllLandCells)
			{
				PlotInfo.StateData.AddState(EPlotState::Plowed);
				int32 InstanceIndex = LandCellRenderer->AddInstance(CellTransform);
				PlotToLandInstanceIndex.Add(PlotIndex, InstanceIndex);
			}
            
			// 记录映射
			
			PlotInfoIndex.Add(FIntPoint(X, Y), PlotInfo);
		}
	}
    
	// 标记渲染数据需要更新
	if (FarmlandConfig->bInitAllLandCells)
	{
		LandCellRenderer->MarkRenderStateDirty();
    
		UE_LOG(LogTemp, Log, TEXT("Initialized %d land cells for %dx%d grid"), 
			   LandCellRenderer->GetInstanceCount(), FarmlandConfig->GridSizeX, FarmlandConfig->GridSizeY);
	}
	
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
		GroundMeshComponent->SetRelativeLocation(FVector(0, 0, GroundExtent.Z));
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

int32 AFarmlandActor::GetPlotIndex(int32 X, int32 Y) const
{
	if (X < 0 || X >= FarmlandConfig->GridSizeX || Y < 0 || Y >= FarmlandConfig->GridSizeY)
	{
		return INDEX_NONE;
	}
	return Y * FarmlandConfig->GridSizeX + X;
}