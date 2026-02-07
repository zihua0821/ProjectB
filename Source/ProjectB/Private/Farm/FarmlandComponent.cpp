// Fill out your copyright notice in the Description page of Project Settings.


#include "Farm/FarmlandComponent.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Farm/FarmlandActor.h"
#include "Tools/ISMTools.h"


// Sets default values for this component's properties
UFarmlandComponent::UFarmlandComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 1.0f; // 每秒更新一次作物生长
	
}

void UFarmlandComponent::HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint PlowCoords)
{
	switch (EditMode)
	{
	case None:
		break;
	case Reset:
		ResetLand(PlowCoords);
		break;
	case Plow:
		PlowLand(PlowCoords);
		break;
	case UnPlow:
		RemoveLand(PlowCoords);
		break;
	case Wet:
		WetLand(PlowCoords);
		break;
	case UnWet:
		LandDried(PlowCoords);
		break;
	case Fertilize:
		FertilizeLand(PlowCoords);
		break;
	case UnFertilize:
		ClearFertilize(PlowCoords);
		break;
	case Weed:
		GenerateWeeds(PlowCoords);
		break;
	case UnWeed:
		ClearWeeds(PlowCoords);
		break;
	case Pest:
		GeneratePest(PlowCoords);
		break;
	case UnPest:
		ClearPest(PlowCoords);
		break;
	case Count:
		break;
	}
}

void UFarmlandComponent::ResetLand(const FIntPoint PlowCoords)
{
	ClearWeeds(PlowCoords);
	ClearPest(PlowCoords);
	RemoveLand(PlowCoords);
}

void UFarmlandComponent::RemoveLand(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}

	const int32 PlotIndex = GetPlotIndex(PlowCoords);
	for (int i = 0; i < FarmlandConfig->NumCustomDataFloats; ++i)
	{
		LandCellRenderer->SetCustomDataValue(PlotIndex, i, 0.f);
	}
	PlotInfo.StateData.Reset();
}

void UFarmlandComponent::PlowLand(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (!PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is not empty"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	const int32 PlotIndex = GetPlotIndex(PlowCoords);
	LandCellRenderer->SetCustomDataValue(PlotIndex, 0, 1.f);
	PlotInfo.StateData.AddState(EPlotState::Plowed);
}

void UFarmlandComponent::WetLand(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (PlotInfo.StateData.HasState(EPlotState::Watered))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already watered"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	const int32 PlotIndex = GetPlotIndex(PlowCoords);
	LandCellRenderer->SetCustomDataValue(PlotIndex, 1, 1.f);
	PlotInfo.StateData.AddState(EPlotState::Watered);
}

void UFarmlandComponent::LandDried(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::Watered))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is not watered"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	const int32 PlotIndex = GetPlotIndex(PlowCoords);
	LandCellRenderer->SetCustomDataValue(PlotIndex, 1, 0.f);
	PlotInfo.StateData.RemoveState(EPlotState::Watered);
}

void UFarmlandComponent::FertilizeLand(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (PlotInfo.StateData.HasState(EPlotState::Fertilized))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already Fertilized"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	const int32 PlotIndex = GetPlotIndex(PlowCoords);
	LandCellRenderer->SetCustomDataValue(PlotIndex, 2, 1.f);
	PlotInfo.StateData.AddState(EPlotState::Fertilized);
}

void UFarmlandComponent::ClearFertilize(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::Fertilized))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is not Fertilized"))
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"))
		return;
	}
	const int32 PlotIndex = GetPlotIndex(PlowCoords);
	LandCellRenderer->SetCustomDataValue(PlotIndex, 2, 0.f);
	PlotInfo.StateData.RemoveState(EPlotState::Fertilized);
}

void UFarmlandComponent::GenerateWeeds(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (PlotInfo.StateData.HasState(EPlotState::WeedInfested))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already WeedInfested"))
		return;
	}
	
	if (WeedsRenderer == nullptr)
	{
		WeedsRenderer = NewObject<UHierarchicalInstancedStaticMeshComponent>(GetOwner());
		WeedsRenderer->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		WeedsRenderer->RegisterComponent();
		WeedsRenderer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeedsRenderer->SetCollisionResponseToAllChannels(ECR_Ignore);
		check(FarmlandConfig->WeedsMesh);
		WeedsRenderer->SetStaticMesh(FarmlandConfig->WeedsMesh);
	}

	const int32 WeedNum = FarmlandConfig->WeedNum;
	const FVector PlotLocation = PlotInfo.Transform.GetLocation();
	const FVector2D PlotSize = FarmlandConfig->CellSize;
	// 中心原点
	const float HalfWidth = PlotSize.X * 0.5f;
	const float HalfHeight = PlotSize.Y * 0.5f;
	// 计算地块实际边界
	const float MinX = PlotLocation.X - HalfWidth;
	const float MaxX = PlotLocation.X + HalfWidth;
	const float MinY = PlotLocation.Y - HalfHeight;
	const float MaxY = PlotLocation.Y + HalfHeight;
    
	// 1. 计算网格划分
	const int32 GridSize = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(WeedNum)));
	const float CellWidth = (MaxX - MinX) / GridSize;
	const float CellHeight = (MaxY - MinY) / GridSize;
    
	// 2. 在每个网格内生成一个杂草
	for (int32 i = 0; i < WeedNum; i++)
	{
		// 计算网格索引
		const int32 GridX = i % GridSize;
		const int32 GridY = i / GridSize;

		const float GridCenterX = MinX + (GridX + 0.5f) * CellWidth;
		const float GridCenterY = MinY + (GridY + 0.5f) * CellHeight;
        
		// 随机偏移
		const float OffsetX = FMath::FRandRange(-CellWidth * 0.4f, CellWidth * 0.4f);
		const float OffsetY = FMath::FRandRange(-CellHeight * 0.4f, CellHeight * 0.4f);

		const float FinalX = FMath::Clamp(GridCenterX + OffsetX, MinX, MaxX);
		const float FinalY = FMath::Clamp(GridCenterY + OffsetY, MinY, MaxY);

		FVector WeedLocation(FinalX, FinalY, PlotLocation.Z);
		
		FTransform InstanceLocalTransform;
		InstanceLocalTransform.SetLocation(WeedLocation);
		
		
		WeedsRenderer->AddInstance(InstanceLocalTransform);
		PlotInfo.WeedsTransform.Add(InstanceLocalTransform);

		
	}
	WeedsRenderer->MarkRenderStateDirty();
 	PlotInfo.StateData.AddState(EPlotState::WeedInfested);
}

void UFarmlandComponent::ClearWeeds(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::WeedInfested) && PlotInfo.WeedsTransform.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is now free of weeds"))
		return;
	}
	if (WeedsRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("WeedsRenderer is nullptr"))
		return;
	}
	for (const FTransform WeedTransform : PlotInfo.WeedsTransform)
	{
		UISMTools::RemoveInstanceByTransform(WeedsRenderer, WeedTransform);
	}
	PlotInfo.WeedsTransform.Empty();
	WeedsRenderer->MarkRenderStateDirty();
	PlotInfo.StateData.RemoveState(EPlotState::WeedInfested);
}

void UFarmlandComponent::GeneratePest(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (PlotInfo.StateData.HasState(EPlotState::PestInfested))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already PestInfested"))
		return;
	}

	check(FarmlandConfig->PestNiagaraSystem)
	const FVector PestLocation = PlotInfo.Transform.GetLocation() + FVector(0.f, 0.f, FarmlandConfig->PestHeight);
	UNiagaraComponent* PestParticle = UNiagaraFunctionLibrary::SpawnSystemAttached(
		FarmlandConfig->PestNiagaraSystem,this, NAME_None,
		PestLocation, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, false);
	FVector PestSize = FVector(FarmlandConfig->CellSize.X / 2.f, FarmlandConfig->CellSize.Y / 2.f, FarmlandConfig->PestHeight / 2.f);
	PestSize *= FarmlandConfig->PestScale;
	PestParticle->SetVectorParameter("PestSize", PestSize);
	PlotInfo.PestNiagaraComponent = PestParticle;
	PlotInfo.StateData.AddState(EPlotState::PestInfested);
}

void UFarmlandComponent::ClearPest(const FIntPoint PlowCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlowCoords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::PestInfested) && PlotInfo.PestNiagaraComponent == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is now free of Pest"))
		return;
	}
	PlotInfo.PestNiagaraComponent->Deactivate();  // 先停用
	PlotInfo.PestNiagaraComponent->DestroyComponent();  // 销毁组件
	PlotInfo.PestNiagaraComponent = nullptr;  // 置空指针
	PlotInfo.StateData.RemoveState(EPlotState::PestInfested);
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
	const FVector LocalPosition = GetComponentTransform().InverseTransformPosition(WorldPosition);
    
	// 计算农场总大小（考虑间距+拓展）
	const float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
	const float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
    
	// 计算本地坐标相对于农场边界的偏移
	const float HalfWidth = TotalWidth * 0.5f;
	const float HalfHeight = TotalHeight * 0.5f;
    
	// 计算归一化坐标 [0, 1]
	const float NormX = (LocalPosition.X + HalfWidth) / TotalWidth;
	const float NormY = (LocalPosition.Y + HalfHeight) / TotalHeight;
    
	// 转换为网格坐标
	const int32 CellX = FMath::FloorToInt(NormX * FarmlandConfig->GridSizeX);
	const int32 CellY = FMath::FloorToInt(NormY * FarmlandConfig->GridSizeY);
    
	// 边界检查
	if (CellX >= 0 && CellX < FarmlandConfig->GridSizeX && CellY >= 0 && CellY < FarmlandConfig->GridSizeY)
	{
		return FIntPoint(CellX, CellY);
	}
    
	// 返回无效坐标
	return FIntPoint(INDEX_NONE, INDEX_NONE);
}

FVector UFarmlandComponent::CellToWorldCoordinates(const int32 CellX, const int32 CellY) const
{
	// 计算农场总大小
	const float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
	const float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension * 2.f;
    
	// 计算从中心开始的偏移
	const float StartX = -TotalWidth * 0.5f + FarmlandConfig->CellSize.X * 0.5f;
	const float StartY = -TotalHeight * 0.5f + FarmlandConfig->CellSize.Y * 0.5f;
    
	// 计算本地位置
	const float LocalX = StartX + CellX * (FarmlandConfig->CellSize.X + FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension);
	const float LocalY = StartY + CellY * (FarmlandConfig->CellSize.Y + FarmlandConfig->CellSpacing + FarmlandConfig->GroundExtension);

	const FVector LocalPosition(LocalX, LocalY, 0);
    
	// 转换到世界空间
	return GetComponentTransform().TransformPosition(LocalPosition);
}

bool UFarmlandComponent::GetClickedCellCoordinates(const FVector& ClickLocation, FIntPoint& OutCellCoords) const
{
	OutCellCoords = WorldToCellCoordinates(ClickLocation);
    
	// 检查坐标是否有效
	if (OutCellCoords.X >= 0 && OutCellCoords.X < FarmlandConfig->GridSizeX && 
		OutCellCoords.Y >= 0 && OutCellCoords.Y < FarmlandConfig->GridSizeY)
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

	InitializeLandCells();
}

void UFarmlandComponent::InitializeLandCells()
{
	ensure(FarmlandConfig);
	ensure(FarmlandConfig->LandCellMesh);

	if (LandCellRenderer != nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCells have been Initialized"));
		return;
	}
	LandCellRenderer = NewObject<UHierarchicalInstancedStaticMeshComponent>(GetOwner());
	LandCellRenderer->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	LandCellRenderer->RegisterComponent();
	LandCellRenderer->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	LandCellRenderer->SetCollisionResponseToAllChannels(ECR_Block);
	check(FarmlandConfig->LandCellMesh);
	LandCellRenderer->SetStaticMesh(FarmlandConfig->LandCellMesh);
	// 设置自定义数据数量 你必须告诉 ISM 组件：“每个实例需要占用多少个浮点位？”
	LandCellRenderer->SetNumCustomDataFloats(FarmlandConfig->NumCustomDataFloats);
	
	// 清除所有现有实例 
	LandCellRenderer->ClearInstances();
	PlotInfoIndex.Empty();
	
	// 计算缩放因子
	// 获取土地网格的原始边界
	const FBoxSphereBounds CellBounds = FarmlandConfig->LandCellMesh->GetBounds();
	const FVector CellExtent = CellBounds.BoxExtent;
	// 计算需要的缩放
	const float ScaleFactorX = FarmlandConfig->CellSize.X / (CellExtent.X * 2.f);
	const float ScaleFactorY = FarmlandConfig->CellSize.Y / (CellExtent.Y * 2.f);
	const float ScaleFactorZ = FarmlandConfig->LandHeightOffset / (CellExtent.Z * 2.f);
	const FVector InstanceScale(ScaleFactorX, ScaleFactorY, ScaleFactorZ);
    
	// 计算总偏移（从中心开始）
	const float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing;
	const float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing;
	const float StartX = -TotalWidth * 0.5f + FarmlandConfig->CellSize.X * 0.5f;
	const float StartY = -TotalHeight * 0.5f + FarmlandConfig->CellSize.Y * 0.5f;
    
	// 创建所有土地单元格实例
	for (int32 Y = 0; Y < FarmlandConfig->GridSizeY; Y++)
	{
		for (int32 X = 0; X < FarmlandConfig->GridSizeX; X++)
		{
			// 计算位置
			const float PosX = StartX + X * (FarmlandConfig->CellSize.X + FarmlandConfig->CellSpacing);
			const float PosY = StartY + Y * (FarmlandConfig->CellSize.Y + FarmlandConfig->CellSpacing);
			const float PosZ = FarmlandConfig->LandHeightOffset / 2.0f;
            
			// 创建变换
			FTransform CellTransform;
			CellTransform.SetLocation(FVector(PosX, PosY, PosZ));
			CellTransform.SetScale3D(InstanceScale);

			// 添加到渲染器
			const int32 InstanceIndex = LandCellRenderer->AddInstance(CellTransform);
			
			// 创建信息
			FFarmLandPlotInfo PlotInfo;
			PlotInfo.Transform = CellTransform;
			
			// 记录映射
			PlotInfoIndex.Add(FIntPoint(X, Y), PlotInfo);
			
			// 初始化隐藏所有地块
			if (FarmlandConfig->bInitAllLandCells)
			{
				PlowLand(FIntPoint(X, Y));
			}
			else
			{
				LandCellRenderer->SetCustomDataValue(InstanceIndex, 0, 0.f);
			}
		}
	}
    
	// 标记渲染数据需要更新
	LandCellRenderer->MarkRenderStateDirty();
    
	UE_LOG(LogTemp, Log, TEXT("Initialized %d land cells for %dx%d grid"), 
		   LandCellRenderer->GetInstanceCount(), FarmlandConfig->GridSizeX, FarmlandConfig->GridSizeY);
	
}

int32 UFarmlandComponent::GetPlotIndex(const FIntPoint& PlotCoords) const
{
	if (PlotCoords.X < 0 || PlotCoords.X >= FarmlandConfig->GridSizeX || PlotCoords.Y < 0 || PlotCoords.Y >= FarmlandConfig->GridSizeY)
	{
		return INDEX_NONE;
	}
	return PlotCoords.Y * FarmlandConfig->GridSizeX + PlotCoords.X;
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
void UFarmlandComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
                                       FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateCrops(DeltaTime);
}

