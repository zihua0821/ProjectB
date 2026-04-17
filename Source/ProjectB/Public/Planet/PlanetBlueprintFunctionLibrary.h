// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PlanetBlueprintFunctionLibrary.generated.h"

class UPlanetConfigDataAsset;
struct FOrbitData;
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
	 * @param CellCoord 单元格坐标
	 * @param OrbitData 轨道数据
	 * @param PlanetActor 行星
	 * @return 轨道角度（度数）
	 */
	static float CalculateOrbitAngleFromCell(const FIntPoint& CellCoord, const FOrbitData& OrbitData, const UObject* PlanetActor);

	/*
	 * 计算单元格坐标对应的轨道角度（度数）
	 * @param CellWorldTransform 单元格世界坐标
	 * @param OrbitData 轨道数据
	 * @return 轨道角度（度数）
	 */
	static float CalculateOrbitAngleFromCell(const FTransform& CellWorldTransform, const FOrbitData& OrbitData);
	
	/*
	 * 获取轨道角度对应的最接近的单元格坐标
	 * @param OrbitAngle 轨道角度（度数）
	 * @param OrbitData 轨道数据
	 * @return CellCoord 单元格坐标
	 */
	static FIntPoint GetNearestCellCoordFromOrbitAngle(float OrbitAngle, const FOrbitData& OrbitData);

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
};
