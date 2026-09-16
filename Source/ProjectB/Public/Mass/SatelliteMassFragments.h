// SatelliteMassFragments.h
// 卫星 Mass 实体数据定义：轨道运动状态 fragment + 卫星标记
// 注意：ESatelliteMoveState 从 OrbitMovementComponent.h 迁出至此（卫星数据层统一），旧组件改为 include 本头
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "SatelliteMassFragments.generated.h"

// 卫星移动状态枚举
UENUM(BlueprintType)
enum class ESatelliteMoveState : uint8
{
	Idle             UMETA(DisplayName = "闲置"),
	MovingToTarget   UMETA(DisplayName = "移动到目标"),
	Paused           UMETA(DisplayName = "暂停")
};

/**
 * 卫星轨道运动状态（核心 fragment）
 * 对应旧 UOrbitMovementComponent 的运行状态字段
 * 注：成员带 UPROPERTY 以便 mass.PrintEntityFragments / 调试工具显示值
 */
USTRUCT()
struct PROJECTB_API FMassOrbitFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 所属轨道名（查 FOrbitData 用） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	FName OrbitName = NAME_None;

	/** 当前相位角（度） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	float CurrentAngle = 0.0f;

	/** 目标角度（度） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	float TargetAngle = 0.0f;

	/** 角速度（度/秒） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	float AngularVelocity = 10.0f;

	/** 运行方向：true=顺时针, false=逆时针 */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	bool bClockwise = false;

	/** 移动状态 */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	ESatelliteMoveState MoveState = ESatelliteMoveState::Idle;
};

/** 卫星实体标记（空 Tag，用于处理器/查询过滤） */
USTRUCT()
struct PROJECTB_API FSatelliteTag : public FMassTag
{
	GENERATED_BODY()
};
