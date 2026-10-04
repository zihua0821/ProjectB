// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PlanetBlueprintFunctionLibrary.generated.h"

class UPlanetConfigDataAsset;
struct FOrbitDefinition;
/**
 * 
 */
UCLASS()
class PROJECTB_API UPlanetBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/*
	 * 计算单元格坐标对应的轨道角度（度数）
	 * @param CellWorldTransform 单元格世界坐标
	 * @param OrbitData 轨道数据
	 * @return 轨道角度（度数）
	 */
	static float CalculateOrbitAngleFromCell(const FTransform& CellWorldTransform, const FOrbitDefinition& OrbitData);
	
	/*
	 * 将世界/局部坐标转换为单元格坐标
	 * @param Location 世界/局部坐标
	 * @param PlanetConfig 行星配置数据
	 * @param IsWorldLocation 是否是世界坐标
	 * @param ComponentToPlanet 组件到行星的变换矩阵
	 * @param Tolerance 容差
	 * @return CellCoord 单元格坐标
	 */
	static FIntPoint ConvertLocationToCellCoord(
		const FVector& Location,
		const UPlanetConfigDataAsset* PlanetConfig,
		const bool IsWorldLocation = false,
		const FTransform& ComponentToPlanet = FTransform::Identity,
		const float Tolerance = 0.01f
		);

	/*
	 * 角度 → 轨道世界位置（平面圆参数化；与旧 UOrbitMovementComponent::CalculateOrbitPosition 同源）
	 * @param OrbitTransform 轨道变换（位置=圆心，旋转=平面朝向）
	 * @param Radius 轨道半径
	 * @param AngleDeg 轨道相位角（度）
	 */
	static FVector ComputeOrbitWorldPosition(const FTransform& OrbitTransform, const float Radius, const float AngleDeg);

	/*
	 * 点到轨道平面的垂直距离（“平面带”覆盖模型的判定基础）
	 */
	static float ComputeDistanceToOrbitPlane(const FVector& Point, const FVector& OrbitCenter, const FVector& OrbitNormal);

	/*
	 * 覆盖判定（平面带）：点到轨道平面垂直距离 ≤ 服务距离
	 * 语义：轨道面为一块“板”（半厚 = 服务距离），球面被板夹住的区域即覆盖区
	 * （与轨道半径无关；与“服务范围圆柱”可视化几何一致）
	 * 选点系统 / 可视化 / 编辑预览共用此判定（唯一权威）
	 */
	static bool IsPointInOrbitCoverage(const FVector& Point, const FVector& OrbitCenter, const FVector& OrbitNormal, const float ServiceDistance);

	// ==================== 离轨作业：路径函数（纯算法；方案见 docs/Mass/卫星离轨作业_需求方案.md v1.0） ====================

	/*
	 * 轨道悬停点（现算，不落存储）：格子中心球面点沿法线外移 HoverHeight
	 * @param CellCenter 格子中心（球心为原点的本地坐标）
	 * @param PlanetRadius 星球半径（cm）
	 * @param HoverHeight 悬停高度（cm；来自 PlanetConfigDataAsset）
	 */
	static FVector ComputeOrbitHoverPoint(const FVector& CellCenter, const float PlanetRadius, const float HoverHeight);

	/*
	 * 空间点 → 轨道平面分解（离轨/回轨轨迹的"平面内 + 垂直"两分量基础）
	 *   OutThetaDeg  平面内方位（度；轨道局部 atan2）
	 *   OutRadius    平面内半径（cm）
	 *   OutVertical  沿轨道法线的偏移量（cm；有符号）
	 * 用途：悬停点作权威点 → 拆出（方位/半径/垂直）→ 轨迹按两分量插值 → 端点精确命中悬停点
	 */
	static void DecomposeToOrbitPlane(const FTransform& OrbitTransform, const FVector& Point, float& OutThetaDeg, float& OutRadius, float& OutVertical);

};
