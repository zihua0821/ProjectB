// PlanetBlueprintFunctionLibrary.cpp
// 实现：星球几何纯函数（声明与规格说明见 .h；覆盖判定唯一权威 IsPointInOrbitCoverage）

#include "Planet/PlanetBlueprintFunctionLibrary.h"

#include "Farm/CellManagerComponent.h"      // GetCellWorldTransform（覆盖格计算）
#include "Mass/OrbitTypes.h"                // FOrbitDefinition（覆盖格计算）
#include "Planet/PlanetConfigDataAsset.h"

float UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(const FTransform& CellWorldTransform,
	const FTransform& OrbitTransform)
{
	// 从轨道变换中获取轨道中心位置和法线（只用变换——不依赖完整轨道数据）
	const FVector OrbitCenter = OrbitTransform.GetLocation();
	const FVector OrbitNormal = OrbitTransform.GetRotation().GetAxisZ();

	// 计算从轨道中心指向单元格位置的方向向量
	const FVector ToCell = CellWorldTransform.GetLocation() - OrbitCenter;
	// 投影到轨道平面（去除法线方向的分量）
	// 注：格子业务坐标不会位于轨道轴向——投影不会退化为零向量（如需严谨可加零向量早退）
	FVector Projected = ToCell - FVector::DotProduct(ToCell, OrbitNormal) * OrbitNormal;
	Projected.Normalize();

	// 获取轨道平面的局部 X 轴方向（轨道零点方向）
	const FVector LocalX = OrbitTransform.GetRotation().GetAxisX(); // X 轴为零点方向

	// 计算投影向量与 X 轴的夹角（度数）
	const float Dot = FVector::DotProduct(Projected, LocalX);
	const float Cross = FVector::DotProduct(OrbitNormal, FVector::CrossProduct(LocalX, Projected)); // 有符号叉积长度
	const float AngleRad = FMath::Atan2(Cross, Dot);
	float AngleDeg = FMath::RadiansToDegrees(AngleRad);
	if (AngleDeg < 0.0f) AngleDeg += 360.0f;

	return AngleDeg;
}

FIntPoint UPlanetBlueprintFunctionLibrary::ConvertLocationToCellCoord(const FVector& Location, const UPlanetConfigDataAsset* PlanetConfig, const bool IsWorldLocation,
	const FTransform& ComponentToPlanet, const float Tolerance)
{
	// 1. 坐标类型处理
	FVector RelativeLocation;
	if (IsWorldLocation)
	{
		RelativeLocation = ComponentToPlanet.TransformPosition(Location);
	}
	else
	{
		RelativeLocation = Location;
	}
    
	// 2. 球面坐标转换
	const float Radius = PlanetConfig->Radius;
	const float LatRad = FMath::Asin(RelativeLocation.Z / Radius);
	const float LonRad = FMath::Atan2(RelativeLocation.Y, RelativeLocation.X);
    
	// 3. 转换为度数
	const float LatDeg = FMath::RadiansToDegrees(LatRad);
	float LonDeg = FMath::RadiansToDegrees(LonRad);
	if (LonDeg < 0.0f) LonDeg += 360.0f;
	
	// 4. 计算经纬度步长
	const float LatStep = (PlanetConfig->MaxLat - PlanetConfig->MinLat) / (PlanetConfig->NumLat - 1);
	const float LonStep = 360.0f / PlanetConfig->NumLon;
    
	// 5. 计算网格索引（考虑精度误差）
	int32 LatIdx = FMath::FloorToInt((LatDeg - PlanetConfig->MinLat + Tolerance) / LatStep);
	int32 LonIdx = FMath::FloorToInt((LonDeg + Tolerance) / LonStep);
    
	// 6. 处理边界情况
	LatIdx = FMath::Clamp(LatIdx, 0, PlanetConfig->NumLat - 2);
	LonIdx = FMath::Clamp(LonIdx, 0, PlanetConfig->NumLon - 1);
	
	return FIntPoint(LatIdx, LonIdx);
}

FVector UPlanetBlueprintFunctionLibrary::ComputeOrbitWorldPosition(const FTransform& OrbitTransform, const float Radius, const float AngleDeg)
{
	const float RadAngle = FMath::DegreesToRadians(AngleDeg);
	const FVector OrbitPlanePosition = FVector(Radius * FMath::Cos(RadAngle), Radius * FMath::Sin(RadAngle), 0.0f);
	return OrbitTransform.TransformPosition(OrbitPlanePosition);
}

float UPlanetBlueprintFunctionLibrary::ComputeDistanceToOrbitPlane(const FVector& Point, const FVector& OrbitCenter, const FVector& OrbitNormal)
{
	return FMath::Abs(FVector::DotProduct(Point - OrbitCenter, OrbitNormal));
}

bool UPlanetBlueprintFunctionLibrary::IsPointInOrbitCoverage(const FVector& Point, const FVector& OrbitCenter, const FVector& OrbitNormal, const float ServiceDistance)
{
	return ComputeDistanceToOrbitPlane(Point, OrbitCenter, OrbitNormal) <= ServiceDistance;
}

void UPlanetBlueprintFunctionLibrary::ComputeCoveredCells(const UCellManagerComponent* CellManager,
	const UPlanetConfigDataAsset& PlanetConfig, const TArray<FOrbitDefinition>& Orbits,
	TMap<FIntPoint, FLinearColor>& OutCells)
{
	OutCells.Reset();

	if (CellManager == nullptr || Orbits.Num() == 0)
	{
		return;
	}

	// 预提取轨道几何与覆盖色（避免内层循环重复计算）
	struct FOrbitGeo
	{
		FVector Center;
		FVector Normal;
		float ServiceDistance = 0.f;
		FLinearColor CoverageColor;
	};
	TArray<FOrbitGeo> OrbitGeos;
	OrbitGeos.Reserve(Orbits.Num());
	for (const FOrbitDefinition& Orbit : Orbits)
	{
		FOrbitGeo Geo;
		Geo.Center = Orbit.Transform.GetLocation();
		Geo.Normal = Orbit.Transform.GetRotation().GetAxisZ();
		Geo.ServiceDistance = Orbit.ServiceDistance;
		Geo.CoverageColor = Orbit.CoverageColor;
		OrbitGeos.Add(Geo);
	}

	// 单次遍历全部格子：收集【全部命中轨道色】求平均（混合色——稳定与列表序无关）；
	// 无命中不入 Map；单轨命中 = 原色（退化一致）。（与选点同源：平面带判定）
	const int32 NumLatCells = PlanetConfig.NumLat - 1;
	const int32 NumLon = PlanetConfig.NumLon;
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
			const FVector CellCenter = CellTransform.GetLocation().GetSafeNormal() * PlanetConfig.Radius;

			FLinearColor SumColor = FLinearColor::Black;
			int32 HitCount = 0;
			for (const FOrbitGeo& Geo : OrbitGeos)
			{
				if (IsPointInOrbitCoverage(CellCenter, Geo.Center, Geo.Normal, Geo.ServiceDistance))
				{
					SumColor += Geo.CoverageColor;
					++HitCount;
				}
			}
			if (HitCount > 0)
			{
				OutCells.Add(Coord, SumColor / static_cast<float>(HitCount));
			}
		}
	}
}

float UPlanetBlueprintFunctionLibrary::NormalizeAngleDegrees360(const float AngleDeg)
{
	const float Remainder = FMath::Fmod(AngleDeg, 360.f);
	return Remainder < 0.f ? Remainder + 360.f : Remainder;
}

// ==================== 离轨作业：路径函数（纯算法） ====================

FVector UPlanetBlueprintFunctionLibrary::ComputeOrbitHoverPoint(const FVector& CellCenter, const float PlanetRadius, const float HoverHeight)
{
	// 球心为原点约定：法线方向 = 归一化位置；悬停点 = 球面点 + 法线 × 悬停高度
	const FVector Radial = CellCenter.GetSafeNormal();
	return Radial * (PlanetRadius + HoverHeight);
}

void UPlanetBlueprintFunctionLibrary::DecomposeToOrbitPlane(const FTransform& OrbitTransform, const FVector& Point, float& OutThetaDeg, float& OutRadius, float& OutVertical)
{
	// 转到轨道局部：局部 XY 平面即轨道平面，局部 Z 即法线方向
	const FVector LocalPoint = OrbitTransform.InverseTransformPosition(Point);
	OutVertical = LocalPoint.Z;
	OutRadius = FVector(LocalPoint.X, LocalPoint.Y, 0.0f).Size();
	OutThetaDeg = FMath::RadiansToDegrees(FMath::Atan2(LocalPoint.Y, LocalPoint.X));
}
