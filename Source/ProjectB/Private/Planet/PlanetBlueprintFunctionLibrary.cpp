// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/PlanetBlueprintFunctionLibrary.h"

#include "Planet/PlanetConfigDataAsset.h"
#include "Planet/PlanetInterface.h"

float UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(const FIntPoint& CellCoord,
                                                                   const FOrbitData& OrbitData, const UObject* PlanetActor)
{
	if (!PlanetActor) return 0.0f;
	const FTransform CellWorldTransform = IPlanetInterface::Execute_GetCellWorldTransform(PlanetActor, CellCoord);
	return CalculateOrbitAngleFromCell(CellWorldTransform, OrbitData);
}

float UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(const FTransform& CellWorldTransform,
	const FOrbitData& OrbitData)
{
	// 从轨道数据中获取轨道中心位置和法线
	const FVector OrbitCenter = OrbitData.Transform.GetLocation();
	const FVector OrbitNormal = OrbitData.Transform.GetRotation().GetUpVector(); 

	// 计算从轨道中心指向单元格位置的方向向量
	const FVector ToCell = CellWorldTransform.GetLocation() - OrbitCenter;
	// 投影到轨道平面（去除法线方向的分量）
	FVector Projected = ToCell - FVector::DotProduct(ToCell, OrbitNormal) * OrbitNormal;
	Projected.Normalize();

	// 获取轨道平面的局部 X 轴方向（轨道零点方向）
	const FVector LocalX = OrbitData.Transform.GetRotation().GetAxisX(); // X 轴为零点方向

	// 计算投影向量与 X 轴的夹角（度数）
	const float Dot = FVector::DotProduct(Projected, LocalX);
	const float Cross = FVector::DotProduct(OrbitNormal, FVector::CrossProduct(LocalX, Projected)); // 有符号叉积长度
	const float AngleRad = FMath::Atan2(Cross, Dot);
	float AngleDeg = FMath::RadiansToDegrees(AngleRad);
	if (AngleDeg < 0.0f) AngleDeg += 360.0f;

	return AngleDeg;
}

FIntPoint UPlanetBlueprintFunctionLibrary::GetNearestCellCoordFromOrbitAngle(const float OrbitAngle,
	const FOrbitData& OrbitData)
{
	float BestDelta = 360.0f;
	FIntPoint BestCell;
	for (const auto& Pair : OrbitData.CoveredCoordsMap)
	{
		const float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(OrbitAngle, Pair.Value));
		if (Delta < BestDelta)
		{
			BestDelta = Delta;
			BestCell = Pair.Key;
		}
	}
	return BestCell;
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
    
	// 3. 球面坐标转换
	const float Radius = PlanetConfig->Radius;
	const float LatRad = FMath::Asin(RelativeLocation.Z / Radius);
	const float LonRad = FMath::Atan2(RelativeLocation.Y, RelativeLocation.X);
    
	// 4. 转换为度数
	const float LatDeg = FMath::RadiansToDegrees(LatRad);
	float LonDeg = FMath::RadiansToDegrees(LonRad);
	if (LonDeg < 0.0f) LonDeg += 360.0f;
	
	// 5. 计算经纬度步长
	const float LatStep = (PlanetConfig->MaxLat - PlanetConfig->MinLat) / (PlanetConfig->NumLat - 1);
	const float LonStep = 360.0f / PlanetConfig->NumLon;
    
	// 6. 计算网格索引（考虑精度误差）
	int32 LatIdx = FMath::FloorToInt((LatDeg - PlanetConfig->MinLat + Tolerance) / LatStep);
	int32 LonIdx = FMath::FloorToInt((LonDeg + Tolerance) / LonStep);
    
	// 7. 处理边界情况
	LatIdx = FMath::Clamp(LatIdx, 0, PlanetConfig->NumLat - 2);
	LonIdx = FMath::Clamp(LonIdx, 0, PlanetConfig->NumLon - 1);
	
	return FIntPoint(LatIdx, LonIdx);
}
