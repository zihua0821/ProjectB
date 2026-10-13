// PlanetBlueprintFunctionLibrary.h
// 星球几何纯函数库：轨道角度/位置换算 · 覆盖判定（唯一权威） · 格坐标转换 · 悬停点 · 平面分解 · 角度归一化
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PlanetBlueprintFunctionLibrary.generated.h"

class UCellManagerComponent;
class UPlanetConfigDataAsset;
struct FOrbitDefinition;

/**
 * 星球几何纯函数库（无状态静态函数集——卫星选点 / 覆盖可视化 / 编辑预览共用）
 */
UCLASS()
class PROJECTB_API UPlanetBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * 计算单元格坐标对应的轨道角度（度数；只用轨道变换——不依赖轨道数据表）
	 * @param CellWorldTransform 单元格世界变换
	 * @param OrbitTransform 轨道变换（相位零点 = 局部 X 轴；角度绕法线测量）
	 * @return 轨道角度（度数，[0,360)）
	 */
	static float CalculateOrbitAngleFromCell(const FTransform& CellWorldTransform, const FTransform& OrbitTransform);
	
	/**
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

	/**
	 * 角度 → 轨道世界位置（平面圆参数化：局部 (R·cosθ, R·sinθ, 0) → 轨道变换）
	 * @param OrbitTransform 轨道变换（位置=圆心，旋转=平面朝向）
	 * @param Radius 轨道半径（cm）
	 * @param AngleDeg 轨道相位角（度）
	 */
	static FVector ComputeOrbitWorldPosition(const FTransform& OrbitTransform, const float Radius, const float AngleDeg);

	/**
	 * 点到轨道平面的垂直距离（“平面带”覆盖模型的判定基础）
	 * @param Point 待判定空间点
	 * @param OrbitCenter 轨道圆心
	 * @param OrbitNormal 轨道平面法线（单位向量）
	 * @return 垂直距离（cm，非负）
	 */
	static float ComputeDistanceToOrbitPlane(const FVector& Point, const FVector& OrbitCenter, const FVector& OrbitNormal);

	/**
	 * 覆盖判定（平面带）：点到轨道平面垂直距离 ≤ 服务距离
	 * 语义：轨道面为一块“板”（半厚 = 服务距离），球面被板夹住的区域即覆盖区
	 * （与轨道半径无关；与“服务范围圆柱”可视化几何一致）
	 * 选点系统 / 可视化 / 编辑预览共用此判定（唯一权威）
	 * @param Point 待判定空间点（通常为格心投影回球面的点）
	 * @param OrbitCenter 轨道圆心
	 * @param OrbitNormal 轨道平面法线（单位向量）
	 * @param ServiceDistance 服务距离（cm；覆盖带半厚）
	 * @return 是否在覆盖区内
	 */
	static bool IsPointInOrbitCoverage(const FVector& Point, const FVector& OrbitCenter, const FVector& OrbitNormal, const float ServiceDistance);

	/**
	 * 覆盖格计算（多轨并集 → 混合色）：遍历全部格，收集【全部命中轨道覆盖色】求平均；
	 * 无命中不入 Map；单轨命中 = 原色；多轨重叠 = 混合色（与轨道列表序无关）
	 * @param CellManager 格子管理器（GetCellWorldTransform 查格心；为空则输出空）
	 * @param PlanetConfig 星球配置（格数/半径）
	 * @param Orbits 运行时轨道（几何 + 覆盖色；调用方已按每轨显示开关过滤）
	 * @param OutCells 输出：坐标 → 混合色
	 */
	static void ComputeCoveredCells(const UCellManagerComponent* CellManager,
		const UPlanetConfigDataAsset& PlanetConfig,
		const TArray<FOrbitDefinition>& Orbits,
		TMap<FIntPoint, FLinearColor>& OutCells);

	/**
	 * 角度归一化到 [0, 360)（消除长时运行累积圈数，保证 FindDeltaAngleDegrees 单次修正准确）
	 * 任务链/规划器共用（原两处 .cpp 匿名实现因 Unity Build 同 TU 重名冲突，已提取为公共函数）
	 * @param AngleDeg 任意角度（度）
	 * @return 归一化角度 [0, 360)
	 */
	static float NormalizeAngleDegrees360(const float AngleDeg);

	// ==================== 离轨作业：路径函数（纯算法；方案见 docs/卫星系统/卫星离轨作业_需求方案.md v1.0） ====================

	/**
	 * 轨道悬停点（现算，不落存储）：格子中心球面点沿法线外移 HoverHeight
	 * 注：作业几何以【理想球面 R】为基准（地块网格 Z 偏移仅作网格偏移，不参与作业几何）
	 * @param CellCenter 格子中心（球心为原点的本地坐标）
	 * @param PlanetRadius 星球半径（cm）
	 * @param HoverHeight 悬停高度（cm；来自卫星配置资产）
	 */
	static FVector ComputeOrbitHoverPoint(const FVector& CellCenter, const float PlanetRadius, const float HoverHeight);

	/**
	 * 空间点 → 轨道平面分解（离轨/回轨轨迹的"平面内 + 垂直"两分量基础）
	 * 用途：悬停点作权威点 → 拆出（方位/半径/垂直）→ 轨迹按两分量插值 → 端点精确命中悬停点
	 * @param OrbitTransform 轨道变换（局部 XY 平面 = 轨道平面；局部 Z = 法线）
	 * @param Point 待分解空间点
	 * @param OutThetaDeg 平面内方位（度；轨道局部 atan2）
	 * @param OutRadius 平面内半径（cm）
	 * @param OutVertical 沿轨道法线的偏移量（cm；有符号）
	 */
	static void DecomposeToOrbitPlane(const FTransform& OrbitTransform, const FVector& Point, float& OutThetaDeg, float& OutRadius, float& OutVertical);

};
