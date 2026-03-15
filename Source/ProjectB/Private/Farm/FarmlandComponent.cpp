// Fill out your copyright notice in the Description page of Project Settings.


#include "Farm/FarmlandComponent.h"

#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Algo/RandomShuffle.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "Player/BasePlayerController.h"
#include "Tools/ISMTools.h"


UFarmlandComponent::UFarmlandComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UFarmlandComponent::HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint Coords)
{
	switch (EditMode)
	{
	case Reset:
		ResetLand(Coords);
		break;
	case Plow:
		PlowLand(Coords);
		break;
	case UnPlow:
		RemoveLand(Coords);
		break;
	case Wet:
		WetLand(Coords);
		break;
	case UnWet:
		LandDried(Coords);
		break;
	case Fertilize:
		FertilizeLand(Coords);
		break;
	case UnFertilize:
		ClearFertilize(Coords);
		break;
	case Weed:
		GenerateWeeds(Coords);
		break;
	case UnWeed:
		ClearWeeds(Coords);
		break;
	case Pest:
		GeneratePest(Coords);
		break;
	case UnPest:
		ClearPest(Coords);
		break;
	default:
		break;
	}
}

void UFarmlandComponent::ResetLand(const FIntPoint Coords)
{
	ClearWeeds(Coords);
	ClearPest(Coords);
	RemoveLand(Coords);
}

void UFarmlandComponent::RemoveLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
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
	if (!UISMTools::RemoveInstanceByTransform(LandCellRenderer, PlotInfo.Transform))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	
	PlotInfo.StateData.Reset();
	PlotInfo.MoisturePercentage = 0.f;
	PlotInfo.FertilityPercentage = 0.f;
	OnPlotStateChanged.Broadcast(Coords, PlotInfo.StateData);
}

void UFarmlandComponent::PlowLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (!PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is not empty"));
		return;
	}
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"));
		return;
	}
	if (int32 PlotIndex; UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot already exists"));
		return;
	}

	LandCellRenderer->AddInstance(PlotInfo.Transform);
	SetPlotState(Coords, EPlotState::Plowed, true);
	UpdatePlotVisual(Coords);
}

void UFarmlandComponent::WetLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
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
	int32 PlotIndex;
	if (!UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	PlotInfo.MoisturePercentage = FMath::Clamp(PlotInfo.MoisturePercentage + FarmlandConfig->IncreasedMoistureWithEachIrrigation, 0.f, 1.f);
	OnPlotMoistureChanged.Broadcast(Coords, PlotInfo.MoisturePercentage);
	SetPlotState(Coords, EPlotState::Watered, true);
	UpdatePlotVisual(Coords);
}

void UFarmlandComponent::LandDried(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
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
	int32 PlotIndex;
	if (!UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	SetPlotState(Coords, EPlotState::Watered, false);
	UpdatePlotVisual(Coords);
}

void UFarmlandComponent::FertilizeLand(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
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
	int32 PlotIndex;
	if (!UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	PlotInfo.FertilityPercentage = FMath::Clamp(PlotInfo.FertilityPercentage + FarmlandConfig->IncreasedFertilityWithEachFertilization, 0.f, 1.f);
	OnPlotFertilityChanged.Broadcast(Coords, PlotInfo.FertilityPercentage);
	SetPlotState(Coords, EPlotState::Fertilized, true);
	UpdatePlotVisual(Coords);
}

void UFarmlandComponent::ClearFertilize(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
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
	int32 PlotIndex;
	if (!UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, PlotIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot not exists"));
		return;
	}
	SetPlotState(Coords, EPlotState::Fertilized, false);
	UpdatePlotVisual(Coords);
}

void UFarmlandComponent::GenerateWeeds(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	// if (PlotInfo.StateData.IsEmpty())
	// {
	// 	UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
	// 	return;
	// }
	if (PlotInfo.StateData.HasState(EPlotState::WeedInfested))
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is already WeedInfested"))
		return;
	}
	
	if (WeedsRenderer == nullptr)
	{
		WeedsRenderer = NewObject<UInstancedStaticMeshComponent>(GetOwner());
		WeedsRenderer->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
		WeedsRenderer->RegisterComponent();
		WeedsRenderer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeedsRenderer->SetCollisionResponseToAllChannels(ECR_Ignore);
		check(FarmlandConfig->WeedsMesh);
		WeedsRenderer->SetStaticMesh(FarmlandConfig->WeedsMesh);
	}

	if (PlotInfo.WeedsTransform.Num() > 0)
	{
		for (FTransform& Transform : PlotInfo.WeedsTransform)
		{
			WeedsRenderer->AddInstance(Transform);
		}
	}
	else
	{
		FBoxSphereBounds CellBounds = FarmlandConfig->LandCellMesh->GetBounds();
		const FVector2D PlotSize = FVector2D(CellBounds.BoxExtent.X * 2.f, CellBounds.BoxExtent.Y * 2.f);      // 地块尺寸

		const int32 WeedNum = FarmlandConfig->WeedNum;
	    const float WeedsScale = FarmlandConfig->WeedsScale;
	    const float WeedsOffsetZ = FarmlandConfig->WeedsOffsetZ;

	    const FTransform& PlotTransform = PlotInfo.Transform;     // 相对于当前组件的变换
	    const FMatrix PlotMatrix = PlotTransform.ToMatrixWithScale();
	    const FVector SurfaceNormal = PlotTransform.GetUnitAxis(EAxis::Z);

	    // 计算网格划分
	    const int32 GridSize = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(WeedNum)));
	    const float CellWidth = PlotSize.X / GridSize;
	    const float CellHeight = PlotSize.Y / GridSize;

	    for (int32 i = 0; i < WeedNum; i++)
	    {
	        // 网格索引
	        const int32 GridX = i % GridSize;
	        const int32 GridY = i / GridSize;

	        // 网格中心在地块局部坐标（地块中心为原点，Z=0）
	        const float LocalCenterX = -PlotSize.X * 0.5f + (GridX + 0.5f) * CellWidth;
	        const float LocalCenterY = -PlotSize.Y * 0.5f + (GridY + 0.5f) * CellHeight;

	        // 动态偏移限制（确保不超出地块局部边界）
	        const float LeftDist  = LocalCenterX + PlotSize.X * 0.5f;
	        const float RightDist = PlotSize.X * 0.5f - LocalCenterX;
	        const float MaxOffsetX = FMath::Min(CellWidth * 0.4f, FMath::Min(LeftDist, RightDist));

	        const float BottomDist = LocalCenterY + PlotSize.Y * 0.5f;
	        const float TopDist    = PlotSize.Y * 0.5f - LocalCenterY;
	        const float MaxOffsetY = FMath::Min(CellHeight * 0.4f, FMath::Min(BottomDist, TopDist));

	        const float OffsetX = (MaxOffsetX > 0.0f) ? FMath::FRandRange(-MaxOffsetX, MaxOffsetX) : 0.0f;
	        const float OffsetY = (MaxOffsetY > 0.0f) ? FMath::FRandRange(-MaxOffsetY, MaxOffsetY) : 0.0f;

	        const float LocalX = LocalCenterX + OffsetX;
	        const float LocalY = LocalCenterY + OffsetY;
	        const FVector LocalWeedPos(LocalX, LocalY, 0.0f);

	        // 转换到当前组件空间（应用地块的旋转、平移、缩放）
	        FVector ComponentLocation = PlotMatrix.TransformPosition(LocalWeedPos);
	        // 沿法线抬升
	        ComponentLocation += SurfaceNormal * WeedsOffsetZ;

	        // 构建在当前组件空间的变换
	        FTransform ComponentTransform;
	        ComponentTransform.SetLocation(ComponentLocation);
	        ComponentTransform.SetScale3D(FVector::One() * WeedsScale);

	        // 旋转：先使Z轴对齐地块法线，再绕法线随机旋转
	        FQuat BaseRotation = PlotTransform.GetRotation();
	        const float RandomYaw = FMath::FRandRange(0.0f, 360.0f);
	        FQuat RandomYawQuat(SurfaceNormal, FMath::DegreesToRadians(RandomYaw));
	        ComponentTransform.SetRotation(RandomYawQuat * BaseRotation);

	    	// 在杂草生成循环中
	    	FTransform WorldTransform = ComponentTransform * GetComponentTransform();
	    	UISMTools::AdjustTransformToGroundWithNormal(WorldTransform, GetWorld(), 1000.0f, true);
	    	ComponentTransform = WorldTransform.GetRelativeTransform(GetComponentTransform());
	    	
	        // 直接添加到 WeedsRenderer（假设其与当前组件共享空间）
	        WeedsRenderer->AddInstance(ComponentTransform);
	        PlotInfo.WeedsTransform.Add(ComponentTransform);
	    }
	}
	
	SetPlotState(Coords, EPlotState::WeedInfested, true);
	StartWeedsSpreadTimers();
}

void UFarmlandComponent::ClearWeeds(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::WeedInfested) && PlotInfo.WeedsTransform.Num() == 0)
	{
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
	SetPlotState(Coords, EPlotState::WeedInfested, false);
	
	TryStopWeedSpreadTimer();
}

void UFarmlandComponent::GeneratePest(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
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
	
	// 获取地块位置和法线方向
	const FTransform& PlotTransform = PlotInfo.Transform;
	const FVector PlotLocation = PlotTransform.GetLocation();
	const FVector SurfaceNormal = PlotTransform.GetUnitAxis(EAxis::Z); // 地块 Z 轴即法线方向
	
	// 沿法线方向偏移（而非世界 Z 轴）
	const FVector PestLocation = PlotLocation + SurfaceNormal * FarmlandConfig->PestHeight;
	
	// 计算粒子朝向（让粒子系统 Z 轴与地块法线对齐）
	const FRotator PestRotation = FRotationMatrix::MakeFromZ(SurfaceNormal).Rotator();
	
	UNiagaraComponent* PestParticle = UNiagaraFunctionLibrary::SpawnSystemAttached(
		FarmlandConfig->PestNiagaraSystem,
		this,
		NAME_None,
		PestLocation,
		PestRotation,  // 使用计算出的朝向
		EAttachLocation::SnapToTarget,
		false);
	
	PestParticle->SetFloatParameter("PestRadius", FarmlandConfig->PestRadius);
	PlotInfo.PestNiagaraComponent = PestParticle;
	SetPlotState(Coords, EPlotState::PestInfested, true);
	
	StartPestsSpreadTimers();
}

void UFarmlandComponent::ClearPest(const FIntPoint Coords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	if (PlotInfo.StateData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Plot is empty"))
		return;
	}
	if (!PlotInfo.StateData.HasState(EPlotState::PestInfested) && PlotInfo.PestNiagaraComponent == nullptr)
	{
		return;
	}
	PlotInfo.PestNiagaraComponent->Deactivate();  // 先停用
	PlotInfo.PestNiagaraComponent->DestroyComponent();  // 销毁组件
	PlotInfo.PestNiagaraComponent = nullptr;  // 置空指针
	SetPlotState(Coords, EPlotState::PestInfested, false);

	TryStopPestSpreadTimer();
}

void UFarmlandComponent::UpdateOutline(FIntPoint Coords, bool bEnable)
{
	if (!PlotInfoIndex.Contains(Coords))
	{
		return;
	}
	const FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	FTransform InstanceTransform = PlotInfo.Transform;
	InstanceTransform.AddToTranslation(FVector(0.f, 0.f, FarmlandConfig->OutlineOffsetZ));
	int32 InstanceIndex;
	UISMTools::GetInstanceIndexByTransform(LandCellOutlineRenderer,InstanceTransform, InstanceIndex);
	if (bEnable)
	{
		LandCellOutlineRenderer->SetCustomDataValue(InstanceIndex, PlotCustomData::Opacity, 0.1f);
		UpdateOutlineColor();
	}
	else
	{
		LandCellOutlineRenderer->SetCustomDataValue(InstanceIndex, PlotCustomData::Opacity, 0.f);
	}
}

void UFarmlandComponent::UpdateOutlineColor() const
{
	if (!LandCellOutlineRenderer)
	{
		UE_LOG(LogTemp, Error, TEXT("OutlineHISMComponent is nullptr"))
		return;
	}
	check(FarmlandConfig->OutlineMaterial);
	UMaterialInstanceDynamic* DynamicOutlineMaterialInstance = LandCellOutlineRenderer->CreateDynamicMaterialInstance(0, FarmlandConfig->OutlineMaterial);
	FLinearColor OutlineColor = FLinearColor::Green;
	switch (BasePlayerController->GetEditMode()) {
	case None:
		OutlineColor = FLinearColor::White;
		break;
	case Reset:
		OutlineColor = FLinearColor::Red;
		break;
	case Plow:
		OutlineColor = FLinearColor::Yellow;
		break;
	case UnPlow:
		OutlineColor = FLinearColor::Red;
		break;
	case Wet:
		OutlineColor = FLinearColor::Blue;
		break;
	case UnWet:
		OutlineColor = FLinearColor::Red;
		break;
	case Fertilize:
		OutlineColor = FLinearColor::Gray;
		break;
	case UnFertilize:
		OutlineColor = FLinearColor::Red;
		break;
	case Weed:
		OutlineColor = FLinearColor::Green;
		break;
	case UnWeed:
		OutlineColor = FLinearColor::Red;
		break;
	case Pest:
		OutlineColor = FLinearColor::Gray;
		break;
	case UnPest:
		OutlineColor = FLinearColor::Red;
		break;
	case EEditMode::PlantCrop:
		OutlineColor = FLinearColor::Yellow;
		break;
	case EEditMode::HarvestCrop:
		OutlineColor = FLinearColor::Yellow;
		break;
	case EEditMode::CropDried:
		OutlineColor = FLinearColor::Gray;
		break;
	case EEditMode::UnCropDried:
		OutlineColor = FLinearColor::Red;
		break;
	case RemoveCrop:
		OutlineColor = FLinearColor::Red;
		break;
	case Count:
		break;
	}
	DynamicOutlineMaterialInstance->SetVectorParameterValue("SelfLuminousColor", OutlineColor);
	LandCellOutlineRenderer->SetMaterial(0,DynamicOutlineMaterialInstance);
}

void UFarmlandComponent::InitLandCellOutlineRenderer()
{
	if (LandCellOutlineRenderer != nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellOutlineRenderer have been Initialized"));
		return;
	}
	LandCellOutlineRenderer = NewObject<UInstancedStaticMeshComponent>(GetOwner());
	LandCellOutlineRenderer->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	LandCellOutlineRenderer->RegisterComponent();
	LandCellOutlineRenderer->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	LandCellOutlineRenderer->SetCollisionResponseToAllChannels(ECR_Ignore);
	ensure(FarmlandConfig);
	ensure(FarmlandConfig->LandCellMesh);
	LandCellOutlineRenderer->SetStaticMesh(FarmlandConfig->LandCellMesh);
	LandCellOutlineRenderer->SetNumCustomDataFloats(1);
	LandCellOutlineRenderer->bDisallowNanite = true;
	LandCellOutlineRenderer->SetCastShadow(false);
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
	// 先通过网格计算获得候选索引
	FIntPoint Candidate = WorldToCellCoordinates(ClickLocation);
	if (Candidate.X == INDEX_NONE || Candidate.Y == INDEX_NONE)
	{
		return false; // 超出农场范围
	}

	// 将世界坐标转换到组件本地空间
	const FVector LocalPos = GetComponentTransform().InverseTransformPosition(ClickLocation);

	// 获取候选地块的边界
	FBox CellBounds;
	if (GetCellBounds(Candidate, CellBounds))
	{
		// 检查点是否在地块内部（包括边界，可根据需求使用 IsInside 或 IsInsideOrOn）
		if (CellBounds.IsInside(LocalPos))
		{
			OutCellCoords = Candidate;
			return true;
		}
	}

	// 点击在间隙中
	return false;
}

bool UFarmlandComponent::GetPlotByRaycast(const FVector& RayStart, const FVector& RayEnd,
	FIntPoint& OutCellCoords, float& OutHitDistance) const
{
	if (!LandCellOutlineRenderer)
	{
		return false;
	}

	const FVector NewRayEnd = FMath::Lerp(RayStart, RayEnd, 1.1f);
	// 使用 ISM 的 HitTest 进行精确检测
	FHitResult HitResult;
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FarmlandClick), false);
    
	// 执行射线检测（针对 ISM 组件）
	if (LandCellOutlineRenderer->LineTraceComponent(HitResult, RayStart, NewRayEnd, QueryParams))
	{
		OutHitDistance = HitResult.Distance;
        
		// 从 HitResult 获取实例索引
		if (HitResult.Item != INDEX_NONE)
		{
			// 通过实例索引反查坐标
			// 需要遍历 PlotInfoIndex 找到匹配的 Transform
			FTransform InstanceTransform;
			if (!LandCellOutlineRenderer->GetInstanceTransform(HitResult.Item, InstanceTransform))
			{
				return false;
			}
            
			for (const auto& PlotInfoTuple : PlotInfoIndex)
			{
				const FTransform& PlotTransform = PlotInfoTuple.Value.Transform;
                
				// 比较位置（允许小误差）
				if (FVector::DistSquared(InstanceTransform.GetLocation(), PlotTransform.GetLocation()) < 1.f)
				{
					OutCellCoords = PlotInfoTuple.Key;
					return true;
				}
			}
		}
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

void UFarmlandComponent::SetPlotState(const FIntPoint& PlotCoords, EPlotState State, bool bEnable)
{
	if (!PlotInfoIndex.Contains(PlotCoords))
	{
		UE_LOG(LogTemp, Warning, TEXT("SetPlotState: Plot %s not found"), *PlotCoords.ToString());
		return;
	}
	
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlotCoords);
	if (bEnable)
	{
		PlotInfo.StateData.AddState(State);
	}
	else
	{
		PlotInfo.StateData.RemoveState(State);
	}
	
	// 广播状态变更事件
	OnPlotStateChanged.Broadcast(PlotCoords, PlotInfo.StateData);
}

TArray<FFarmLandPlotSaveGameData> UFarmlandComponent::GetPlotSaveGameData()
{
	PlotSaveGameData.Empty();
	for (const auto& PlotInfoTuple : PlotInfoIndex)
	{
		// 创建保存数据项
		FFarmLandPlotSaveGameData PlotSaveGameDataItem;
		PlotSaveGameDataItem.Coords = PlotInfoTuple.Key;
		PlotSaveGameDataItem.StateData = PlotInfoTuple.Value.StateData;
		PlotSaveGameDataItem.WeedsTransform = PlotInfoTuple.Value.WeedsTransform;
		PlotSaveGameDataItem.MoisturePercentage = PlotInfoTuple.Value.MoisturePercentage;
		PlotSaveGameDataItem.FertilityPercentage = PlotInfoTuple.Value.FertilityPercentage;
		PlotSaveGameData.Add(PlotSaveGameDataItem);
	}
	return PlotSaveGameData;
}

void UFarmlandComponent::BeginPlay()
{
	Super::BeginPlay();

	BasePlayerController = Cast<ABasePlayerController>(GetWorld()->GetFirstPlayerController());
	if (BasePlayerController)
	{
		BasePlayerController->OnEditModeChanged.AddDynamic(this, &ThisClass::OnEditModeChanged);
	}
	InitializeLandCells();

	StartPestsSpreadTimers();
	StartWeedsSpreadTimers();
}

void UFarmlandComponent::UpdatePlotLoss(float DeltaTime)
{
	if (!FarmlandConfig)
	{
		return;
	}

	// 计算本帧的自然流失量
	const float MoistureLoss = FarmlandConfig->NaturalMoistureLossPerSecond * DeltaTime;
	const float FertilityLoss = FarmlandConfig->NaturalFertilityLossPerSecond * DeltaTime;

	if (FMath::IsNearlyZero(MoistureLoss) && FMath::IsNearlyZero(FertilityLoss))
	{
		return;
	}

	for (auto& PlotInfoTuple : PlotInfoIndex)
	{
		FFarmLandPlotInfo& PlotInfo = PlotInfoTuple.Value;
		const FIntPoint Coords = PlotInfoTuple.Key;
		
		// 更新水分
		if (PlotInfo.MoisturePercentage > 0.f)
		{
			float OldMoisture = PlotInfo.MoisturePercentage;
			PlotInfo.MoisturePercentage = FMath::Clamp(PlotInfo.MoisturePercentage - MoistureLoss, 0.f, 1.f);
			
			if (!FMath::IsNearlyEqual(OldMoisture, PlotInfo.MoisturePercentage))
			{
				OnPlotMoistureChanged.Broadcast(Coords, PlotInfo.MoisturePercentage);
				HandlePlotMoistureChanged(Coords);
			}
		}

		// 更新肥力
		if (PlotInfo.FertilityPercentage > 0.f)
		{
			float OldFertility = PlotInfo.FertilityPercentage;
			PlotInfo.FertilityPercentage = FMath::Clamp(PlotInfo.FertilityPercentage - FertilityLoss, 0.f, 1.f);

			if (!FMath::IsNearlyEqual(OldFertility, PlotInfo.FertilityPercentage))
			{
				OnPlotFertilityChanged.Broadcast(Coords, PlotInfo.FertilityPercentage);
				HandlePlotFertilityChanged(Coords);
			}
		}
	}
}

void UFarmlandComponent::TickComponent(float DeltaTime, enum ELevelTick TickType,
                                       FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdatePlotLoss(DeltaTime);
}

void UFarmlandComponent::CreateDefaultLandCells()
{
    // 计算缩放因子
	const FBoxSphereBounds CellBounds = FarmlandConfig->LandCellMesh->GetBounds();
	const FVector CellExtent = CellBounds.BoxExtent;
	const float BaseScaleFactorX = FarmlandConfig->CellSize.X / (CellExtent.X * 2.f);
	const float ScaleFactorY = FarmlandConfig->CellSize.Y / (CellExtent.Y * 2.f);
	const float ScaleFactorZ = FarmlandConfig->LandHeightOffset / (CellExtent.Z * 2.f);

	// 纬度收缩参数
	const float MinLatitudeScale = 0.5f;  // 边缘最小缩放系数（Y 轴缩到 70%）

    // 自动计算经纬度步长
    const float LatStep = (PlanetConfig->MaxLat - PlanetConfig->MinLat) / (PlanetConfig->NumLat - 1);
    const float LonStep = 360.0f / PlanetConfig->NumLon;

	const int32 NumLatCells = PlanetConfig->NumLat - 1;  
	const int32 NumLonCells = PlanetConfig->NumLon;       
	
    // 遍历经纬度网格生成地块
    for (int32 LatIdx = 0; LatIdx <NumLatCells; LatIdx++)
    {
        for (int32 LonIdx = 0; LonIdx < NumLonCells; LonIdx++)
        {
            // 将索引转换为弧度
        	const float LatRad = FMath::DegreesToRadians(PlanetConfig->MinLat + (LatIdx + 0.5f) * LatStep);
        	const float LonRad = FMath::DegreesToRadians((LonIdx + 0.5f) * LonStep);

            // 球面坐标转世界坐标（Z 轴向上）
            const float CosLat = FMath::Cos(LatRad);
            const float SinLat = FMath::Sin(LatRad);
            const float CosLon = FMath::Cos(LonRad);
            const float SinLon = FMath::Sin(LonRad);

            FVector PlotLocation(
                PlanetConfig->Radius * CosLat * CosLon,
                PlanetConfig->Radius * CosLat * SinLon,
                PlanetConfig->Radius * SinLat
            );

        	// 计算地块朝向：让 Z 轴指向球心外（径向）
        	const FVector NormalDirection = PlotLocation.GetSafeNormal();
        	PlotLocation += NormalDirection * FarmlandConfig->CellOffsetZ;
        	const FRotator PlotRotation = FRotationMatrix::MakeFromZ(NormalDirection).Rotator();

        	// 计算纬度缩放系数（基于纬度角，Z 轴是上下方向）
        	const float CurrentLatDeg = PlanetConfig->MinLat + (LatIdx + 0.5f) * LatStep;
        	const float AbsLatFromEquator = FMath::Abs(CurrentLatDeg);  // 距离赤道的纬度
        	const float MaxLatFromEquator = FMath::Max(FMath::Abs(PlanetConfig->MinLat), FMath::Abs(PlanetConfig->MaxLat));
        	const float LatitudeFactor = FMath::Clamp(AbsLatFromEquator / MaxLatFromEquator, 0.f, 1.f);
        	const float LatitudeScale = FMath::Lerp(1.0f, MinLatitudeScale, LatitudeFactor);

        	// 应用纬度收缩（只缩 Y 轴）
        	FTransform PlotTransform;
        	PlotTransform.SetLocation(PlotLocation);
        	PlotTransform.SetRotation(PlotRotation.Quaternion());
        	PlotTransform.SetScale3D(FVector(BaseScaleFactorX * LatitudeScale, ScaleFactorY, ScaleFactorZ));

            // 记录地块信息
            FFarmLandPlotInfo PlotInfo;
            PlotInfo.Transform = PlotTransform;
            PlotInfoIndex.Add(FIntPoint(LatIdx, LonIdx), PlotInfo);

        	// FVector WorldLocation = GetComponentTransform().TransformPosition(PlotLocation);
        	// WorldLocation += NormalDirection * 100.f;
        	// DrawDebugPoint(GetWorld(), WorldLocation, 15.f, FColor::Emerald, true, -1.f, 0);
        	// const FString DebugText = FString::Printf(TEXT("(%d,%d)"), LatIdx, LonIdx);
        	// DrawDebugString(GetWorld(), WorldLocation, DebugText, nullptr, FColor::White, -1.f, false, 2.f);


        	// 创建轮廓
        	FTransform OutlineInstanceTransform = PlotInfo.Transform;
        	OutlineInstanceTransform.AddToTranslation(FVector(0.f, 0.f, FarmlandConfig->OutlineOffsetZ));
        	int32 OutlineInstanceIdx = LandCellOutlineRenderer->AddInstance(OutlineInstanceTransform);
        	LandCellOutlineRenderer->SetCustomDataValue(OutlineInstanceIdx, PlotCustomData::Opacity, 0.f);
        	
            // 初始化耕地状态
            if (FarmlandConfig->bInitAllLandCells && PlotSaveGameData.Num() == 0)
            {
                PlowLand(FIntPoint(LatIdx, LonIdx));
            }
        }
    }
	
	UE_LOG(LogTemp, Log, TEXT("Initialized %d land cells on sphere (%dx%d grid, total %d)"),
	   LandCellRenderer->GetInstanceCount(), NumLatCells, NumLonCells, NumLatCells * NumLonCells);
}

bool UFarmlandComponent::GetCellBounds(const FIntPoint& Coords, FBox& OutBounds) const
{
	if (!FarmlandConfig || Coords.X < 0 || Coords.X >= FarmlandConfig->GridSizeX || Coords.Y < 0 || Coords.Y >= FarmlandConfig->GridSizeY)
	{
		return false;
	}

	// 计算农场总尺寸
	const float TotalWidth = FarmlandConfig->GridSizeX * FarmlandConfig->CellSize.X + 
							 (FarmlandConfig->GridSizeX - 1) * FarmlandConfig->CellSpacing + 
							 FarmlandConfig->GroundExtension * 2.f;
	const float TotalHeight = FarmlandConfig->GridSizeY * FarmlandConfig->CellSize.Y + 
							  (FarmlandConfig->GridSizeY - 1) * FarmlandConfig->CellSpacing + 
							  FarmlandConfig->GroundExtension * 2.f;
	const float HalfWidth = TotalWidth * 0.5f;
	const float HalfHeight = TotalHeight * 0.5f;

	// 计算地块左下角（本地坐标）
	float Left = -HalfWidth + FarmlandConfig->GroundExtension + 
				 Coords.X * (FarmlandConfig->CellSize.X + FarmlandConfig->CellSpacing);
	float Bottom = -HalfHeight + FarmlandConfig->GroundExtension + 
				   Coords.Y * (FarmlandConfig->CellSize.Y + FarmlandConfig->CellSpacing); // 假设 Y 向上

	float Right = Left + FarmlandConfig->CellSize.X;
	float Top = Bottom + FarmlandConfig->CellSize.Y;

	// 返回包围盒（Z 轴范围放宽，假设地块在 Z=0 附近）
	OutBounds = FBox(FVector(Left, Bottom, -100.f), FVector(Right, Top, 100.f));
	return true;
}

void UFarmlandComponent::InitPlotFromSaveGameData()
{
	if (LandCellRenderer == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"));
		return;
	}

	if (PlotSaveGameData.Num() > 0)
	{
		for (auto& PlotSaveGameDataItem : PlotSaveGameData)
		{
			FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlotSaveGameDataItem.Coords);
			PlotInfo.WeedsTransform = PlotSaveGameDataItem.WeedsTransform;
			PlotInfo.MoisturePercentage = PlotSaveGameDataItem.MoisturePercentage;
			PlotInfo.FertilityPercentage = PlotSaveGameDataItem.FertilityPercentage;
			PlotInfo.StateData = PlotSaveGameDataItem.StateData;
			if (PlotInfo.StateData.HasState(EPlotState::Plowed))
			{
				LandCellRenderer->AddInstance(PlotInfo.Transform);
				UpdatePlotVisual(PlotSaveGameDataItem.Coords);
			}
			if (PlotSaveGameDataItem.StateData.HasState(EPlotState::WeedInfested))
			{
				GenerateWeeds(PlotSaveGameDataItem.Coords);
			}
			if (PlotSaveGameDataItem.StateData.HasState(EPlotState::PestInfested))
			{
				GeneratePest(PlotSaveGameDataItem.Coords);
			}
		}
	}
}

void UFarmlandComponent::UpdatePlotVisual(const FIntPoint& PlotCoords)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(PlotCoords);
	if (!LandCellRenderer)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer is nullptr"));
		return;
	}
	int32 Index;
	if (!UISMTools::GetInstanceIndexByTransform(LandCellRenderer, PlotInfo.Transform, Index))
	{
		UE_LOG(LogTemp, Error, TEXT("Can't find instance index by transform"));
		return;
	}

	LandCellRenderer->SetCustomDataValue(
		Index,
		PlotCustomData::Opacity,
		PlotInfo.StateData.HasState(EPlotState::Plowed) ? 1.0f : 0.0f
	);
	LandCellRenderer->SetCustomDataValue(
		Index,
		PlotCustomData::Wetness,
		PlotInfo.StateData.HasState(EPlotState::Watered) ? 1.0f : 0.0f
	);
	LandCellRenderer->SetCustomDataValue(
		Index,
		PlotCustomData::Fertility,
		PlotInfo.StateData.HasState(EPlotState::Fertilized) ? 1.0f : 0.0f
	);
	
}

void UFarmlandComponent::InitLandCellRenderer()
{
	if (LandCellRenderer != nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("LandCellRenderer have been Initialized"));
		return;
	}
	LandCellRenderer = NewObject<UInstancedStaticMeshComponent>(GetOwner());
	LandCellRenderer->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
	LandCellRenderer->RegisterComponent();
	LandCellRenderer->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	LandCellRenderer->SetCollisionResponseToAllChannels(ECR_Block);
	LandCellRenderer->SetCollisionObjectType(ECC_WorldDynamic);
	ensure(FarmlandConfig);
	ensure(FarmlandConfig->LandCellMesh);
	LandCellRenderer->SetStaticMesh(FarmlandConfig->LandCellMesh);
	// 设置自定义数据数量 你必须告诉 ISM 组件：“每个实例需要占用多少个浮点位？”
	LandCellRenderer->SetNumCustomDataFloats(FarmlandConfig->NumCustomDataFloats);
}

void UFarmlandComponent::InitializeLandCells()
{
	InitLandCellRenderer();
	InitLandCellOutlineRenderer();

	CreateDefaultLandCells();
	InitPlotFromSaveGameData();
}

int32 UFarmlandComponent::GetPlotIndex(const FIntPoint& PlotCoords) const
{
	if (PlotCoords.X < 0 || PlotCoords.X >= FarmlandConfig->GridSizeX || PlotCoords.Y < 0 || PlotCoords.Y >= FarmlandConfig->GridSizeY)
	{
		return INDEX_NONE;
	}
	return PlotCoords.Y * FarmlandConfig->GridSizeX + PlotCoords.X;
}

// ReSharper disable once CppMemberFunctionMayBeConst
void UFarmlandComponent::OnEditModeChanged(EEditMode NewEditMode)
{
	UpdateOutlineColor();
}

void UFarmlandComponent::HandlePlotMoistureChanged(FIntPoint Coords)
{
	const FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	// 判断水分状态
	if (PlotInfo.MoisturePercentage >= 0.2f && PlotInfo.MoisturePercentage <= 0.8f)// 正常
	{
		UE_LOG(LogTemp, Log, TEXT("Moisture level is normal: %.2f"), PlotInfo.MoisturePercentage);
	}
	else if (PlotInfo.MoisturePercentage > 0.8f)// 过高
	{
		UE_LOG(LogTemp, Log, TEXT("Moisture level is excessive: %.2f"), PlotInfo.MoisturePercentage);
	}
	else// 过低
	{
		UE_LOG(LogTemp, Log, TEXT("Moisture level is insufficient: %.2f"), PlotInfo.MoisturePercentage);
		LandDried(Coords);
	}
}

void UFarmlandComponent::HandlePlotFertilityChanged(FIntPoint Coords)
{
	const FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	// 判断肥力状态
	if (PlotInfo.FertilityPercentage >= 0.2f && PlotInfo.FertilityPercentage <= 0.8f)// 正常
	{
		UE_LOG(LogTemp, Log, TEXT("Fertility level is normal: %.2f"), PlotInfo.FertilityPercentage);
	}
	else if (PlotInfo.FertilityPercentage > 0.8f)// 过高
	{
		UE_LOG(LogTemp, Log, TEXT("Fertility level is excessive: %.2f"), PlotInfo.FertilityPercentage);
	}
	else// 过低
	{
		UE_LOG(LogTemp, Log, TEXT("Fertility level is insufficient: %.2f"), PlotInfo.FertilityPercentage);
		ClearFertilize(Coords);
	}
}

void UFarmlandComponent::OnCropGrownThisTime(FIntPoint Coords, float MoistureCost, float FertilityCost)
{
	FFarmLandPlotInfo& PlotInfo = PlotInfoIndex.FindChecked(Coords);
	PlotInfo.MoisturePercentage = FMath::Clamp(PlotInfo.MoisturePercentage - MoistureCost, 0.f, 1.f);
	PlotInfo.FertilityPercentage = FMath::Clamp(PlotInfo.FertilityPercentage - FertilityCost, 0.f, 1.f);
	OnPlotMoistureChanged.Broadcast(Coords, PlotInfo.MoisturePercentage);
	OnPlotFertilityChanged.Broadcast(Coords, PlotInfo.FertilityPercentage);
	HandlePlotMoistureChanged(Coords);
	HandlePlotFertilityChanged(Coords);
}

void UFarmlandComponent::StartWeedsSpreadTimers()
{
	if (GetWorld()->GetTimerManager().IsTimerActive(WeedSpreadTimerHandle))
	{
		return;
	}
	if (FarmlandConfig)
	{
		GetWorld()->GetTimerManager().SetTimer(WeedSpreadTimerHandle, this, &ThisClass::SpreadWeeds,
												FarmlandConfig->WeedSpreadInterval, true);
	}
}

void UFarmlandComponent::StartPestsSpreadTimers()
{
	if (GetWorld()->GetTimerManager().IsTimerActive(PestSpreadTimerHandle))
	{
		return;
	}
	if (FarmlandConfig)
	{
		GetWorld()->GetTimerManager().SetTimer(PestSpreadTimerHandle, this, &ThisClass::SpreadPests,
												FarmlandConfig->PestSpreadInterval, true);
	}
}

bool UFarmlandComponent::TryStopWeedSpreadTimer()
{
	// 收集当前所有有杂草的地块
	TArray<FIntPoint> InfectedPlots;
	for (const auto& Pair : PlotInfoIndex)
	{
		if (Pair.Value.StateData.HasState(EPlotState::WeedInfested))
		{
			InfectedPlots.Add(Pair.Key);
		}
	}

	// 如果没有被感染的地块，则停止定时器
	if (InfectedPlots.Num() == 0)
	{
		GetWorld()->GetTimerManager().ClearTimer(WeedSpreadTimerHandle);
		return true;
	}
	return false;
}

void UFarmlandComponent::SpreadWeeds()
{
	if (!FarmlandConfig) return;

	if (TryStopWeedSpreadTimer()) return;

	// 获取所有被感染的地块
	TArray<FIntPoint> InfectedPlots;
	for (const auto& Pair : PlotInfoIndex)
	{
		if (Pair.Value.StateData.HasState(EPlotState::WeedInfested))
		{
			InfectedPlots.Add(Pair.Key);
		}
	}
	// 随机顺序处理，避免偏向性
	Algo::RandomShuffle(InfectedPlots);
	
	for (const FIntPoint& Source : InfectedPlots)
	{
		TArray<FIntPoint> Neighbors;
		GetNeighborCoords(Source, Neighbors);

		for (const FIntPoint& Target : Neighbors)
		{
			FFarmLandPlotInfo& TargetPlot = PlotInfoIndex.FindChecked(Target);

			// 如果目标地块已经感染，跳过
			if (TargetPlot.StateData.HasState(EPlotState::WeedInfested))
				continue;

			// 检查是否可以传播到目标（根据配置和地块状态）
			bool bCanSpread = true;
			if (!FarmlandConfig->bWeedCanSpreadToEmptyPlots)
			{
				// 需要目标地块已耕地
				if (!TargetPlot.StateData.HasState(EPlotState::Plowed))
					bCanSpread = false;
			}

			if (bCanSpread && FMath::FRand() < FarmlandConfig->WeedSpreadProbability)
			{
				// 调用现有的生成杂草方法（该方法会更新状态和视觉）
				GenerateWeeds(Target);
			}
		}
	}
}

bool UFarmlandComponent::TryStopPestSpreadTimer()
{
	// 收集所有有虫害的地块
	TArray<FIntPoint> InfestedPlots;
	for (const auto& Pair : PlotInfoIndex)
	{
		if (Pair.Value.StateData.HasState(EPlotState::PestInfested))
		{
			InfestedPlots.Add(Pair.Key);
		}
	}

	// 如果没有被感染的地块，则停止定时器
	if (InfestedPlots.Num() == 0)
	{
		GetWorld()->GetTimerManager().ClearTimer(PestSpreadTimerHandle);
		return true;
	}
	return false;
}

void UFarmlandComponent::SpreadPests()
{
	if (!FarmlandConfig) return;

	if (TryStopPestSpreadTimer()) return;

	// 收集所有有虫害的地块
	TArray<FIntPoint> InfestedPlots;
	for (const auto& Pair : PlotInfoIndex)
	{
		if (Pair.Value.StateData.HasState(EPlotState::PestInfested))
		{
			InfestedPlots.Add(Pair.Key);
		}
	}
	// 随机顺序处理，避免偏向性
	Algo::RandomShuffle(InfestedPlots);

	for (const FIntPoint& Source : InfestedPlots)
	{
		TArray<FIntPoint> Neighbors;
		GetNeighborCoords(Source, Neighbors);

		for (const FIntPoint& Target : Neighbors)
		{
			FFarmLandPlotInfo& TargetPlot = PlotInfoIndex.FindChecked(Target);

			if (TargetPlot.StateData.HasState(EPlotState::PestInfested))
				continue;

			// 虫害传播条件：通常需要目标有作物，或至少已耕地
			bool bCanSpread = true;
			if (!FarmlandConfig->bPestCanSpreadToEmptyPlots)
			{
				// 要求目标地块有作物
				if (TargetPlot.StateData.HasState(EPlotState::CropPlanted))
					bCanSpread = false;
			}

			if (bCanSpread && FMath::FRand() < FarmlandConfig->PestSpreadProbability)
			{
				GeneratePest(Target);
			}
		}
	}
}

void UFarmlandComponent::GetNeighborCoords(FIntPoint Coords, TArray<FIntPoint>& OutNeighbors) const
{
	// 八个方向偏移量：上下左右 + 四个对角线
	const FIntPoint Directions[] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1), // 四邻域
		FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1) // 对角线
	};

	for (const FIntPoint& Dir : Directions)
	{
		FIntPoint Neighbor = Coords + Dir;
		// 边界检查
		if (Neighbor.X >= 0 && Neighbor.X < FarmlandConfig->GridSizeX &&
			Neighbor.Y >= 0 && Neighbor.Y < FarmlandConfig->GridSizeY)
		{
			OutNeighbors.Add(Neighbor);
		}
	}
}

