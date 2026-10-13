// OrbitTypes.h
// 轨道领域公共类型（唯一轨道结构：FOrbitDefinition——几何 + 运行时合并；N9 配置体系重整产物）
// 消费者：卫星管理器 / 任务链 / 规划层 / 编辑面板（PC + Slate）/ 覆盖可视化组件
// 边界：评分权重随卫星配置资产头；规划类型留 OrbitPlanningTypes.h；渲染句柄随 ISMManager.h
#pragma once

#include "CoreMinimal.h"
#include "OrbitTypes.generated.h"

/**
 * 轨道定义（项目唯一权威结构——几何 4 字段 + 运行时 5 字段合并）
 *
 * 几何四参数模型：
 * - OrbitName        轨道名称（唯一标识）
 * - Transform        轨道变换：
 *                      · Location：轨道圆心（可任意偏移，不要求过球心）
 *                      · Rotation：定义轨道平面朝向与相位零点（角度起点）
 *                          - Roll  (X)：轨道方位
 *                          - Pitch (Y)：轨道倾角
 *                          - Yaw   (Z)：相位零点（轨道起点）
 *                    （三维中确定一个平面只需两个自由度；剩余的一个自由度
 *                      即"轨道起点"——卫星角度参数化的基准方向）
 * - Radius           轨道半径（cm）
 * - ServiceDistance  服务距离（cm）——覆盖“圆柱体”的半高：
 *                      覆盖区 = 管（中线=轨道圆、半高=服务距离）∩ 球面；
 *                      默认 1000；配 0 时由运行时兜底为默认值
 *
 * 运行时字段（编辑系统可调；重建后生效到卫星实体）：
 * - SatelliteCount / AngularVelocity / bClockwise：该轨卫星参数
 * - VisualColor / CoverageColor：轨道视觉与覆盖高亮颜色
 */
USTRUCT(BlueprintType)
struct PROJECTB_API FOrbitDefinition
{
	GENERATED_BODY()

	/** 轨道名称（唯一标识） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (DisplayName = "轨道名称"))
	FName OrbitName = NAME_None;

	/** 轨道变换（Location=圆心；Rotation=平面朝向与相位零点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (DisplayName = "轨道变换"))
	FTransform Transform = FTransform::Identity;

	/** 轨道半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (DisplayName = "轨道半径(cm)"))
	float Radius = 55000.f;

	/** 服务距离（cm；覆盖圆柱半高；默认 1000，配 0 时由运行时兜底） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (DisplayName = "服务距离(cm)"))
	float ServiceDistance = 1000.f;

	// ---- 运行时字段（编辑系统可调） ----

	/** 该轨道的卫星数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "0"))
	int32 SatelliteCount = 3;

	/** 默认角速度（度/秒；创建/重建卫星时写入其 fragment——运行时以卫星自身为准） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	float AngularVelocity = 10.f;

	/** 默认运行方向（创建/重建卫星时应用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	bool bClockwise = false;

	/** 轨道视觉颜色（轨道环/范围圆柱；默认按轨道序自动分配；面板可改） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	FLinearColor VisualColor = FLinearColor(0.10f, 0.70f, 0.95f, 1.0f);

	/** 覆盖格高亮颜色（默认按轨道序自动分配；面板可改；多轨道覆盖冲突时取列表序靠前者） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	FLinearColor CoverageColor = FLinearColor(1.00f, 0.75f, 0.10f, 1.0f);
};
