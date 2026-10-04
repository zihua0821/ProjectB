// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/PlanetBlueprintFunctionLibrary.h"

#include "Planet/OrbitManagerComponent.h"    // FOrbitDefinition（轨道定义权威结构）
#include "Planet/PlanetConfigDataAsset.h"

float UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(const FTransform& CellWorldTransform,
	const FOrbitDefinition& OrbitData)
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
