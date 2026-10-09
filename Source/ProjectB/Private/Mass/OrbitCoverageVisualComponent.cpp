// OrbitCoverageVisualComponent.cpp
// 渲染经 ISMManager（单桶路径）：环段/圆柱均为 ISM 实例；颜色经实例 CustomData（RGBA）传递
#include "Mass/OrbitCoverageVisualComponent.h"

#include "Farm/CellManagerComponent.h"
#include "Mass/CellMassFragments.h"            // FHighlightFragment（覆盖格高亮）
#include "Mass/SatelliteManagerComponent.h"    // FRuntimeOrbitData / USatelliteManagerComponent
#include "Mass/SatelliteMassFragments.h"       // 卫星 fragment/shared
#include "Planet/PlanetBlueprintFunctionLibrary.h"   // IsPointInOrbitCoverage（判定唯一权威）
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Planet/PlanetActor.h"
#include "Planet/PlanetConfigDataAsset.h"
#include "Components/InstancedStaticMeshComponent.h"   // GetBucketComponent 返回类型（环材质实例）
#include "Components/StaticMeshComponent.h"            // 圆柱（独立静态网格组件）
#include "Materials/MaterialInstanceDynamic.h"         // 组件级材质实例（颜色）
#include "Subsystem/ISMManager.h"              // 通用 ISM 管理器（实例增删改）

namespace
{
	/** 由平面法线构造平面内正交基 */
	void BuildPlaneBasis(const FVector& Normal, FVector& OutE1, FVector& OutE2)
	{
		const FVector N = Normal.GetSafeNormal();
		const FVector Reference = FMath::Abs(N.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector;
		OutE1 = FVector::CrossProduct(Reference, N).GetSafeNormal();
		OutE2 = FVector::CrossProduct(N, OutE1);
	}
}

UOrbitCoverageVisualComponent::UOrbitCoverageVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetComponentTickEnabled(false);

	// 网格资源（RingSegmentMesh / CylinderMeshAsset）在细节面板配置；材质取自网格资产默认材质
}

void UOrbitCoverageVisualComponent::Initialize(APlanetActor* InPlanetActor)
{
	PlanetActor = InPlanetActor;
	if (!PlanetActor.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[OrbitVisual] Initialize failed: PlanetActor is null"));
		return;
	}

	CellManager = PlanetActor->GetCellManagerComponent();
	SatelliteManager = PlanetActor->GetSatelliteManagerComponent();

	// 订阅轨道数据变化（编辑拖动 → 置脏 → Tick 节流重建）
	if (SatelliteManager)
	{
		SatelliteManager->OnRuntimeOrbitChanged.AddDynamic(this, &UOrbitCoverageVisualComponent::HandleRuntimeOrbitChanged);
	}
}

void UOrbitCoverageVisualComponent::SetVisualizationEnabled(const bool bEnabled)
{
	if (bVisualizationEnabled == bEnabled)
	{
		return;
	}
	bVisualizationEnabled = bEnabled;
	SetComponentTickEnabled(bEnabled);

	if (bEnabled)
	{
		// 网格资源缺失提示（组件细节面板配置；尺寸自动读取）
		if (RingSegmentMesh == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[OrbitVisual] RingSegmentMesh is not configured - set it in component details"));
		}
		if (CylinderMeshAsset == nullptr)
		{
			UE_LOG(LogTemp, Warning, TEXT("[OrbitVisual] CylinderMeshAsset is not configured - set it in component details"));
		}
		RefreshVisualization();
	}
	else
	{
		// 关闭：回收全部 ISM 实例（空桶自动停用）+ 清除覆盖格高亮
		RemoveAllVisualInstances();
		if (bEnableCellHighlight)
		{
			RebuildCellHighlight(TMap<FIntPoint, FLinearColor>());
		}
	}
}

void UOrbitCoverageVisualComponent::RefreshVisualization()
{
	if (!SatelliteManager)
	{
		return;
	}

	const TArray<FRuntimeOrbitData>& Orbits = SatelliteManager->GetRuntimeOrbits();

	// 1. 清理已不存在轨道的实例（ISM 管理器回收 + 条目移除）
	{
		TSet<FName> CurrentNames;
		CurrentNames.Reserve(Orbits.Num());
		for (const FRuntimeOrbitData& Orbit : Orbits)
		{
			CurrentNames.Add(Orbit.OrbitName);
		}

		for (auto It = OrbitVisuals.CreateIterator(); It; ++It)
		{
			if (!CurrentNames.Contains(It.Key()))
			{
				RemoveOrbitVisualInstances(It.Key());
				It.RemoveCurrent();
			}
		}
	}

	// 2. 逐轨道重建（环段 + 圆柱；段数不变时仅做 ById 增量更新）
	for (const FRuntimeOrbitData& Orbit : Orbits)
	{
		RebuildOrbitVisual(Orbit.OrbitName);
	}

	// 3. 覆盖格高亮（多轨道并集 → 增删/颜色 diff）——覆盖的权威表达
	if (bEnableCellHighlight)
	{
		TMap<FIntPoint, FLinearColor> NewCoveredCells;
		ComputeCoveredCells(Orbits, NewCoveredCells);
		RebuildCellHighlight(NewCoveredCells);
	}
}

void UOrbitCoverageVisualComponent::HandleRuntimeOrbitChanged(FName OrbitName)
{
	bPendingRebuild = true;
}

void UOrbitCoverageVisualComponent::RemoveOrbitVisualInstances(const FName OrbitName)
{
	FOrbitVisualEntry* Entry = OrbitVisuals.Find(OrbitName);
	if (Entry == nullptr)
	{
		return;
	}

	// 环：回收 ISM 实例（空桶自动停用）
	if (RingSegmentMesh && Entry->RingInstanceIds.Num() > 0)
	{
		UWorld* World = GetWorld();
		UISMManager* ISMManager = World ? World->GetSubsystem<UISMManager>() : nullptr;
		if (ISMManager)
		{
			ISMManager->BatchRemoveInstances(RingSegmentMesh, Entry->RingInstanceIds);
		}
		Entry->RingInstanceIds.Reset();
		Entry->RingSegmentCount = 0;
	}

	// 圆柱：销毁独立组件
	if (Entry->CylinderMesh)
	{
		Entry->CylinderMesh->DestroyComponent();
		Entry->CylinderMesh = nullptr;
		Entry->CylinderMID = nullptr;
	}
}

void UOrbitCoverageVisualComponent::RemoveAllVisualInstances()
{
	for (const TPair<FName, FOrbitVisualEntry>& Pair : OrbitVisuals)
	{
		RemoveOrbitVisualInstances(Pair.Key);
	}
}

void UOrbitCoverageVisualComponent::RebuildOrbitVisual(const FName OrbitName)
{
	if (!SatelliteManager || !PlanetActor.IsValid() || !PlanetActor->PlanetConfig)
	{
		return;
	}

	const FRuntimeOrbitData* Orbit = SatelliteManager->GetRuntimeOrbits().FindByPredicate(
		[OrbitName](const FRuntimeOrbitData& Data) { return Data.OrbitName == OrbitName; });
	if (Orbit == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	UISMManager* ISMManager = World ? World->GetSubsystem<UISMManager>() : nullptr;
	if (ISMManager == nullptr)
	{
		return;
	}

	const FVector OrbitCenter = Orbit->Transform.GetLocation();
	const FVector OrbitNormal = Orbit->Transform.GetRotation().GetAxisZ();
	const float PlanetRadius = PlanetActor->PlanetConfig->Radius;

	// 单桶路径（可视化对象实例少；与格子的球面分桶隔离）
	FISMComponentParams ISMParams;
	ISMParams.AttachParent = this;
	ISMParams.bDisableBuckets = true;
	ISMParams.bCastShadow = false;

	FOrbitVisualEntry& Entry = OrbitVisuals.FindOrAdd(OrbitName);

	// ---- 轨道圆环（N 段；段数不变 → ById 增量更新；段数变化 → 重建实例组） ----
	if (RingSegmentMesh)
	{
		const FVector SegmentMeshExtent = RingSegmentMesh->GetBounds().BoxExtent;
		if (SegmentMeshExtent.X > KINDA_SMALL_NUMBER && SegmentMeshExtent.Y > KINDA_SMALL_NUMBER && SegmentMeshExtent.Z > KINDA_SMALL_NUMBER)
		{
			const int32 SegmentCount = FMath::Max(8, RingSegmentCount);
			const float SegmentAngleRad = 2.f * PI / static_cast<float>(SegmentCount);
			const float ChordLength = 2.f * Orbit->Radius * FMath::Sin(SegmentAngleRad * 0.5f);
			// 段间隙：段长 = 弦长 × (1 − 间隙比例)——间隙 0 = 首尾相接（实心圆），0.3 = 留 30% 空隙（虚线感）
			const float SegmentLength = ChordLength * (1.f - RingSegmentGap);

			FVector E1, E2;
			BuildPlaneBasis(OrbitNormal, E1, E2);

			TArray<FTransform> SegmentTransforms;
			SegmentTransforms.Reserve(SegmentCount);
			for (int32 Index = 0; Index < SegmentCount; ++Index)
			{
				const float Theta = SegmentAngleRad * static_cast<float>(Index);
				const float CosTheta = FMath::Cos(Theta);
				const float SinTheta = FMath::Sin(Theta);

				const FVector Radial = E1 * CosTheta + E2 * SinTheta;
				const FVector Tangent = -E1 * SinTheta + E2 * CosTheta;
				const FVector Position = OrbitCenter + Radial * Orbit->Radius;
				const FQuat Rotation = FRotationMatrix::MakeFromZ(Tangent).Rotator().Quaternion();
				// 线粗/段长 换算为缩放（线粗 = 直径 → 除以网格 XY 全尺寸 2×Extent）
				const FVector Scale(
					RingLineThickness / (SegmentMeshExtent.X * 2.f),
					RingLineThickness / (SegmentMeshExtent.Y * 2.f),
					SegmentLength / (SegmentMeshExtent.Z * 2.f));

				SegmentTransforms.Add(FTransform(Rotation, Position, Scale));
			}

			if (Entry.RingInstanceIds.Num() != SegmentCount)
			{
				// 段数变化（或首次）：全删 + 批量添加
				if (Entry.RingInstanceIds.Num() > 0)
				{
					ISMManager->BatchRemoveInstances(RingSegmentMesh, Entry.RingInstanceIds);
					Entry.RingInstanceIds.Reset();
				}

				Entry.RingInstanceIds = ISMManager->BatchAddInstances(
					RingSegmentMesh,
					SegmentTransforms,
					TArray<FIntPoint>(),        // 空间分桶依据：空 → 不启用（单桶）
					0,                          // 逻辑分桶依据：默认组（圆环不分组）
					0,                          // 无 CustomData（颜色走组件级材质实例）
					ISMParams,
					/*bWorldSpace=*/false);     // 相对本组件（星球本地空间）
				Entry.RingSegmentCount = SegmentCount;
			}
			else
			{
				// 段数不变：ById 增量更新变换
				ISMManager->BatchUpdateInstancesTransforms(RingSegmentMesh, Entry.RingInstanceIds, SegmentTransforms, /*bWorldSpace=*/false);
			}

			// 组件级材质实例（从 ISMManager 公开接口获取组件；同一网格共享组件 → 全局统一色）
			if (RingMID == nullptr)
			{
				if (UInstancedStaticMeshComponent* RingISM = ISMManager->GetBucketComponent(RingSegmentMesh, 0))
				{
					RingMID = RingISM->CreateDynamicMaterialInstance(0, RingMaterial);
					if (RingMID)
					{
						RingMID->SetVectorParameterValue(TEXT("BaseColor"), RingColor);
					}
				}
			}

			UE_LOG(LogTemp, Verbose, TEXT("[OrbitVisual] Orbit '%s': ring updated (%d segments, Radius=%.1f)"),
				*OrbitName.ToString(), SegmentCount, Orbit->Radius);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[OrbitVisual] Orbit '%s': ring segment mesh has invalid bounds"), *OrbitName.ToString());
		}
	}

	// −− 服务范围圆柱（独立静态网格组件：每轨道一个；不占 ISM 实例——单实例无需分桶/句柄） −−
	if (CylinderMeshAsset)
	{
		const FVector CylinderExtent = CylinderMeshAsset->GetBounds().BoxExtent;
		if (CylinderExtent.X > KINDA_SMALL_NUMBER && CylinderExtent.Y > KINDA_SMALL_NUMBER && CylinderExtent.Z > KINDA_SMALL_NUMBER)
		{
			// 圆柱半径 = max(轨道半径, 星球半径 × 兜底系数)：保证"盘"包住球面（与平面带判定一致）
			const float CylinderRadius = FMath::Max(Orbit->Radius, PlanetRadius * CylinderRadiusScale);
			const FTransform CylinderTransform(
				FRotationMatrix::MakeFromZ(OrbitNormal).Rotator(),
				OrbitCenter,
				FVector(
					CylinderRadius / CylinderExtent.X,
					CylinderRadius / CylinderExtent.Y,
					Orbit->ServiceDistance / CylinderExtent.Z));

			// 首次创建组件（每轨道独立）
			if (Entry.CylinderMesh == nullptr)
			{
				Entry.CylinderMesh = NewObject<UStaticMeshComponent>(this);
				Entry.CylinderMesh->SetupAttachment(this);
				Entry.CylinderMesh->SetMobility(EComponentMobility::Movable);
				Entry.CylinderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Entry.CylinderMesh->SetStaticMesh(CylinderMeshAsset);
				Entry.CylinderMesh->RegisterComponent();
			}

			Entry.CylinderMesh->SetRelativeLocation(CylinderTransform.GetLocation());
			Entry.CylinderMesh->SetRelativeRotation(CylinderTransform.GetRotation());
			Entry.CylinderMesh->SetRelativeScale3D(CylinderTransform.GetScale3D());
			Entry.CylinderMesh->SetVisibility(bVisualizationEnabled);

			// 每轨道独立材质实例（按轨道色——圆柱可分色）
			if (Entry.CylinderMID == nullptr)
			{
				Entry.CylinderMID = Entry.CylinderMesh->CreateDynamicMaterialInstance(0, CylinderMaterial);
			}
			if (Entry.CylinderMID)
			{
				Entry.CylinderMID->SetVectorParameterValue(TEXT("BaseColor"), Orbit->VisualColor);
			}

			UE_LOG(LogTemp, Verbose, TEXT("[OrbitVisual] Orbit '%s': cylinder updated (Radius=%.1f, HalfHeight=%.1f)"),
				*OrbitName.ToString(), CylinderRadius, Orbit->ServiceDistance);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[OrbitVisual] Orbit '%s': cylinder mesh has invalid bounds"), *OrbitName.ToString());
		}
	}
}

void UOrbitCoverageVisualComponent::ComputeCoveredCells(const TArray<FRuntimeOrbitData>& Orbits, TMap<FIntPoint, FLinearColor>& OutCells) const
{
	OutCells.Reset();

	if (!CellManager || !PlanetActor.IsValid() || !PlanetActor->PlanetConfig || Orbits.Num() == 0)
	{
		return;
	}

	const UPlanetConfigDataAsset& Config = *PlanetActor->PlanetConfig;

	// 预提取轨道几何（避免内层循环重复计算）
	struct FOrbitGeo
	{
		FVector Center;
		FVector Normal;
		float ServiceDistance = 0.f;
		FLinearColor CoverageColor;
	};
	TArray<FOrbitGeo> OrbitGeos;
	OrbitGeos.Reserve(Orbits.Num());
	for (const FRuntimeOrbitData& Orbit : Orbits)
	{
		FOrbitGeo Geo;
		Geo.Center = Orbit.Transform.GetLocation();
		Geo.Normal = Orbit.Transform.GetRotation().GetAxisZ();
		Geo.ServiceDistance = Orbit.ServiceDistance;
		Geo.CoverageColor = Orbit.CoverageColor;
		OrbitGeos.Add(Geo);
	}

	// 单次遍历全部格子：任一轨道覆盖即计入（与选点判定同源：平面带）
	// 多轨道覆盖冲突：列表序靠前者优先（颜色归属前者）
	const int32 NumLatCells = Config.NumLat - 1;
	const int32 NumLon = Config.NumLon;
	for (int32 LatIdx = 0; LatIdx < NumLatCells; ++LatIdx)
	{
		for (int32 LonIdx = 0; LonIdx < NumLon; ++LonIdx)
		{
			const FIntPoint Coord(LatIdx, LonIdx);

			FTransform CellTransform;
			if (!CellManager->GetCellWorldTransform(Coord, CellTransform))
			{
				continue;
			}
			// 与选点一致：格子中心投影回球面点后判定
			const FVector CellCenter = CellTransform.GetLocation().GetSafeNormal() * Config.Radius;

			for (const FOrbitGeo& Geo : OrbitGeos)
			{
				if (UPlanetBlueprintFunctionLibrary::IsPointInOrbitCoverage(CellCenter, Geo.Center, Geo.Normal, Geo.ServiceDistance))
				{
					OutCells.Add(Coord, Geo.CoverageColor);
					break;
				}
			}
		}
	}
}

void UOrbitCoverageVisualComponent::RebuildCellHighlight(const TMap<FIntPoint, FLinearColor>& NewCoveredCells)
{
	if (!CellManager)
	{
		return;
	}
	FMassEntityManager& EntityManager = GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();

	// 移除：旧有新无 → 取消高亮
	for (const TPair<FIntPoint, FLinearColor>& Pair : CurrentCoveredCells)
	{
		if (NewCoveredCells.Contains(Pair.Key))
		{
			continue;
		}
		FMassEntityHandle Entity;
		if (!CellManager->GetCellEntity(Pair.Key, Entity))
		{
			continue;
		}
		if (FHighlightFragment* Highlight = EntityManager.GetFragmentDataPtr<FHighlightFragment>(Entity))
		{
			Highlight->bHighlighted = false;
		}
	}

	// 新增 / 颜色变化：标记高亮（使用归属轨道的覆盖色）
	for (const TPair<FIntPoint, FLinearColor>& Pair : NewCoveredCells)
	{
		const FLinearColor* OldColor = CurrentCoveredCells.Find(Pair.Key);
		const bool bNewlyCovered = (OldColor == nullptr);
		const bool bColorChanged = (OldColor != nullptr) && (*OldColor != Pair.Value);
		if (!bNewlyCovered && !bColorChanged)
		{
			continue;
		}
		FMassEntityHandle Entity;
		if (!CellManager->GetCellEntity(Pair.Key, Entity))
		{
			continue;
		}
		if (FHighlightFragment* Highlight = EntityManager.GetFragmentDataPtr<FHighlightFragment>(Entity))
		{
			Highlight->bHighlighted = true;
			Highlight->HighlightColor = Pair.Value;
		}
	}

	CurrentCoveredCells = NewCoveredCells;

	UE_LOG(LogTemp, Verbose, TEXT("[OrbitVisual] Covered cells: %d"), CurrentCoveredCells.Num());
}

void UOrbitCoverageVisualComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bVisualizationEnabled || !bPendingRebuild)
	{
		return;
	}

	TimeSinceRebuild += DeltaTime;
	if (TimeSinceRebuild >= RebuildInterval)
	{
		TimeSinceRebuild = 0.f;
		bPendingRebuild = false;
		RefreshVisualization();
	}
}
