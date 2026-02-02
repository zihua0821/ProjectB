// Fill out your copyright notice in the Description page of Project Settings.


#include "Farm/FarmlandComponent.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Farm/FarmlandActor.h"


// Sets default values for this component's properties
UFarmlandComponent::UFarmlandComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 1.0f; // 每秒更新一次作物生长
    

	// 初始化土地单元格
	// InitializeLandCells();
}

void UFarmlandComponent::SetFarmLandData(const FFarmLandData& InFarmLandData)
{
	FarmLandData = InFarmLandData;
}

void UFarmlandComponent::PlowLand(FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (!PlotInfo.StateData.IsEmpty())
	{
		return;
	}
	PlotInfo.StateData.AddState(EPlotState::Plowed);
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> PlowedLandRenderer = nullptr;
	if (LandRendererMap.Contains(EPlotState::Plowed))
	{
		PlowedLandRenderer = LandRendererMap.FindChecked(EPlotState::Plowed);
	}
	else
	{
		PlowedLandRenderer = NewObject<UHierarchicalInstancedStaticMeshComponent>(GetOwner());
		PlowedLandRenderer->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		PlowedLandRenderer->RegisterComponent();
		PlowedLandRenderer->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		PlowedLandRenderer->SetCollisionResponseToAllChannels(ECR_Block);
		check(FarmLandData.LandCellMesh);
		PlowedLandRenderer->SetStaticMesh(FarmLandData.LandCellMesh);
		LandRendererMap.Add(EPlotState::Plowed, PlowedLandRenderer);
	}
	PlotInfo.InstanceIndex = PlowedLandRenderer->AddInstance(PlotInfo.Transform);
	PlowedLandRenderer->MarkRenderStateDirty();
}

bool UFarmlandComponent::PlantCrop(int32 PlotX, int32 PlotY, FName CropID)
{
	return true;
}

bool UFarmlandComponent::HarvestCrop(int32 PlotX, int32 PlotY)
{
	return true;

}

UCropBase* UFarmlandComponent::GetCropAtPlot(int32 PlotX, int32 PlotY) const
{
	return nullptr;
}

void UFarmlandComponent::UpdateCrops(float DeltaTime)
{
}

FIntPoint UFarmlandComponent::WorldToCellCoordinates(const FVector& WorldPosition) const
{
	// 将世界坐标转换到组件的本地空间
	FVector LocalPosition = GetComponentTransform().InverseTransformPosition(WorldPosition);
    
	// 计算农场总大小（考虑间距+拓展）
	float TotalWidth = FarmLandData.GridSizeX * FarmLandData.CellSize.X + (FarmLandData.GridSizeX - 1) * FarmLandData.CellSpacing + FarmLandData.GroundExtension * 2.f;
	float TotalHeight = FarmLandData.GridSizeY * FarmLandData.CellSize.Y + (FarmLandData.GridSizeY - 1) * FarmLandData.CellSpacing + FarmLandData.GroundExtension * 2.f;
    
	// 计算本地坐标相对于农场边界的偏移
	float HalfWidth = TotalWidth * 0.5f;
	float HalfHeight = TotalHeight * 0.5f;
    
	// 计算归一化坐标 [0, 1]
	float NormX = (LocalPosition.X + HalfWidth) / TotalWidth;
	float NormY = (LocalPosition.Y + HalfHeight) / TotalHeight;
    
	// 转换为网格坐标
	int32 CellX = FMath::FloorToInt(NormX * FarmLandData.GridSizeX);
	int32 CellY = FMath::FloorToInt(NormY * FarmLandData.GridSizeY);
    
	// 边界检查
	if (CellX >= 0 && CellX < FarmLandData.GridSizeX && CellY >= 0 && CellY < FarmLandData.GridSizeY)
	{
		return FIntPoint(CellX, CellY);
	}
    
	// 返回无效坐标
	return FIntPoint(INDEX_NONE, INDEX_NONE);
}

FVector UFarmlandComponent::CellToWorldCoordinates(int32 CellX, int32 CellY) const
{
	// 计算农场总大小
	float TotalWidth = FarmLandData.GridSizeX * FarmLandData.CellSize.X + (FarmLandData.GridSizeX - 1) * FarmLandData.CellSpacing + FarmLandData.GroundExtension * 2.f;
	float TotalHeight = FarmLandData.GridSizeY * FarmLandData.CellSize.Y + (FarmLandData.GridSizeY - 1) * FarmLandData.CellSpacing + FarmLandData.GroundExtension * 2.f;
    
	// 计算从中心开始的偏移
	float StartX = -TotalWidth * 0.5f + FarmLandData.CellSize.X * 0.5f;
	float StartY = -TotalHeight * 0.5f + FarmLandData.CellSize.Y * 0.5f;
    
	// 计算本地位置
	float LocalX = StartX + CellX * (FarmLandData.CellSize.X + FarmLandData.CellSpacing + FarmLandData.GroundExtension);
	float LocalY = StartY + CellY * (FarmLandData.CellSize.Y + FarmLandData.CellSpacing + FarmLandData.GroundExtension);
    
	FVector LocalPosition(LocalX, LocalY, 0);
    
	// 转换到世界空间
	return GetComponentTransform().TransformPosition(LocalPosition);
}

bool UFarmlandComponent::GetClickedCellCoordinates(const FVector& ClickLocation, FIntPoint& OutCellCoords) const
{
	OutCellCoords = WorldToCellCoordinates(ClickLocation);
    
	// 检查坐标是否有效
	if (OutCellCoords.X >= 0 && OutCellCoords.X < FarmLandData.GridSizeX && 
		OutCellCoords.Y >= 0 && OutCellCoords.Y < FarmLandData.GridSizeY)
	{
		return true;
	}
    
	return false;
}

FFarmLandPlotInfo UFarmlandComponent::GetPlotInfo(const FIntPoint& PlotCoords) const
{
	if (PlotInfoIndex.Contains(PlotCoords))
	{
		return *PlotInfoIndex.Find(PlotCoords);
	}
	return FFarmLandPlotInfo();
}

void UFarmlandComponent::BeginPlay()
{
	Super::BeginPlay();

	// 确保土地单元格已初始化
	// InitializeLandCells();
}

void UFarmlandComponent::OnComponentCreated()
{
	Super::OnComponentCreated();

	// InitializeLandCells();
}

void UFarmlandComponent::OnRegister()
{
	Super::OnRegister();
	
	// InitializeLandCells();
}


void UFarmlandComponent::InitializeLandCells()
{
	
	
}

int32 UFarmlandComponent::GetPlotIndex(int32 X, int32 Y) const
{
	if (X < 0 || X >= FarmLandData.GridSizeX || Y < 0 || Y >= FarmLandData.GridSizeY)
	{
		return INDEX_NONE;
	}
	return Y * FarmLandData.GridSizeX + X;
}

FVector UFarmlandComponent::GetPlotWorldPosition(int32 X, int32 Y) const
{
	return FVector::ZeroVector;
}

void UFarmlandComponent::UpdateCropVisual(int32 PlotIndex, UCropBase* Crop)
{
}

TObjectPtr<UHierarchicalInstancedStaticMeshComponent> UFarmlandComponent::GetOrCreateRenderer(FName CropID)
{
	return nullptr;
}


// Called every frame
void UFarmlandComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                       FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateCrops(DeltaTime);
}

