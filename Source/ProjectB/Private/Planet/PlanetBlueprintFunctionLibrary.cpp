// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/PlanetBlueprintFunctionLibrary.h"

#include "Planet/OrbitManagerComponent.h"    // FOrbitDefinition（轨道定义权威结构）
#include "Planet/PlanetConfigDataAsset.h"
#include "Planet/PlanetInterface.h"

float UPlanetBlueprintFunctionLibrary::CalculateOrbitAngleFromCell(const FIntPoint& CellCoord,
                                                                   const FOrbitDefinition& OrbitData, const UObject* PlanetActor)
{
	if (!PlanetActor) return 0.0f;
	const FTransform CellWorldTransform = IPlanetInterface::Execute_GetCellWorldTransform(PlanetActor, CellCoord);
	return CalculateOrbitAngleFromCell(CellWorldTransform, OrbitData);
}

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

FVector UPlanetBlueprintFunctionLibrary::ComputeOrbitTangent(const FTransform& OrbitTransform, const float AngleDeg, const bool bClockwise)
{
	// 与 ComputeOrbitWorldPosition 同一参数化：局部 (R cosθ, R sinθ, 0)
	// 单位切向（θ 增大方向）的局部形式 = (-sinθ, cosθ, 0)
	const float RadAngle = FMath::DegreesToRadians(AngleDeg);
	const FVector LocalTangent(-FMath::Sin(RadAngle), FMath::Cos(RadAngle), 0.0f);

	// 顺时针 = 角度递减 → 运动方向取反
	const float Direction = bClockwise ? -1.0f : 1.0f;
	return OrbitTransform.TransformVectorNoScale(LocalTangent * Direction).GetSafeNormal();
}

bool UPlanetBlueprintFunctionLibrary::IsAtNearestPhase(const FTransform& OrbitTransform, const float CurrentAngleDeg, const FVector& TargetLocation, const float ToleranceDeg)
{
	const FVector OrbitCenter = OrbitTransform.GetLocation();
	const FVector OrbitNormal = OrbitTransform.GetRotation().GetAxisZ();

	// 当前相位方向（平面内单位径向）
	const float RadAngle = FMath::DegreesToRadians(CurrentAngleDeg);
	const FVector CurrentDir = OrbitTransform.TransformVectorNoScale(FVector(FMath::Cos(RadAngle), FMath::Sin(RadAngle), 0.0f)).GetSafeNormal();

	// 目标方向投影到轨道平面
	const FVector ToTarget = TargetLocation - OrbitCenter;
	const FVector TargetDir = (ToTarget - FVector::DotProduct(ToTarget, OrbitNormal) * OrbitNormal).GetSafeNormal();

	// 退化：目标在轨道轴上（投影为零）——任意相位等距，视为到达
	if (TargetDir.IsNearlyZero())
	{
		return true;
	}

	const float CosAngle = FVector::DotProduct(CurrentDir, TargetDir);
	return CosAngle >= FMath::Cos(FMath::DegreesToRadians(ToleranceDeg));
}

FVector UPlanetBlueprintFunctionLibrary::EvaluateCubicBezier(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, const float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
	const float Smoothed = T * T * (3.0f - 2.0f * T);   // smoothstep 缓动
	const float U = 1.0f - Smoothed;
	return U * U * U * P0 + 3.0f * U * U * Smoothed * P1 + 3.0f * U * Smoothed * Smoothed * P2 + Smoothed * Smoothed * Smoothed * P3;
}

void UPlanetBlueprintFunctionLibrary::BuildOrbitTransferControlPoints(const FVector& StartPoint, const FVector& EndPoint, const FVector& OrbitTangent,
	const bool bOutbound, const float ControlFactor, FVector& OutP1, FVector& OutP2)
{
	const float ControlDistance = FMath::Max(0.0f, ControlFactor) * FVector::Dist(StartPoint, EndPoint);
	const FVector StartRadial = StartPoint.GetSafeNormal();   // 起点法线方向
	const FVector EndRadial = EndPoint.GetSafeNormal();       // 终点法线方向

	if (bOutbound)
	{
		// 离轨：出发沿轨道切向；到达沿终点法线垂直下降（从格子上方落下）
		OutP1 = StartPoint + OrbitTangent * ControlDistance;
		OutP2 = EndPoint + EndRadial * ControlDistance;
	}
	else
	{
		// 回轨：起点垂直上升；末端沿轨道切向切入（入轨切向对齐自然成立）
		OutP1 = StartPoint + StartRadial * ControlDistance;
		OutP2 = EndPoint + OrbitTangent * ControlDistance;
	}
}

bool UPlanetBlueprintFunctionLibrary::ValidateTransferPath(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3,
	const float MinRadius, const int32 SampleCount, const float Tolerance)
{
	const int32 Steps = FMath::Max(2, SampleCount);
	for (int32 Index = 0; Index <= Steps; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(Steps);
		const FVector Position = EvaluateCubicBezier(P0, P1, P2, P3, Alpha);
		if (Position.Size() < MinRadius - Tolerance)
		{
			return false;
		}
	}
	return true;
}
