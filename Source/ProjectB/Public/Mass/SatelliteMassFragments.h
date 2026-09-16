// SatelliteMassFragments.h
// 卫星 Mass 实体数据定义：轨道运动状态 fragment + 卫星标记
// 注意：ESatelliteMoveState 从 OrbitMovementComponent.h 迁出至此（卫星数据层统一），旧组件改为 include 本头
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "Subsystem/ISMManager.h"   // FGlobalInstanceId（卫星渲染实例句柄）
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

/**
 * 卫星渲染状态：ISM 实例句柄（由 SatelliteManager 注册时回写，渲染同步处理器消费）
 */
USTRUCT()
struct PROJECTB_API FSatelliteRenderFragment : public FMassFragment
{
	GENERATED_BODY()

	/** ISM 实例稳定句柄 */
	FGlobalInstanceId InstanceId;
};

/**
 * 卫星共享数据（ConstSharedFragment，按值去重：同轨道+同 Mesh 的卫星共享一份）
 * - 轨道几何：角度→世界位置的计算基础（与原 CalculateOrbitPosition 同源）
 * - 渲染 Mesh：渲染同步处理器按此更新 ISM 实例
 */
USTRUCT()
struct PROJECTB_API FMassSatelliteSharedFragment : public FMassConstSharedFragment
{
	GENERATED_BODY()

	/** 轨道变换（平面方向与中心点） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	FTransform OrbitTransform = FTransform::Identity;

	/** 轨道半径（cm） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float Radius = 30000.0f;

	/** 卫星网格体（渲染用） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	TObjectPtr<UStaticMesh> SatelliteMesh = nullptr;

	/** 卫星缩放倍率（渲染更新时保持与注册时一致） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float SatelliteScale = 1.0f;
};

/**
 * 角度 → 轨道世界位置（与原 UOrbitMovementComponent::CalculateOrbitPosition 同源）
 * 平面圆参数化后经轨道变换转换到世界空间
 */
FORCEINLINE FVector ComputeOrbitWorldPosition(const FTransform& OrbitTransform, const float Radius, const float AngleDeg)
{
	const float RadAngle = FMath::DegreesToRadians(AngleDeg);
	const FVector OrbitPlanePosition = FVector(Radius * FMath::Cos(RadAngle), Radius * FMath::Sin(RadAngle), 0.0f);
	return OrbitTransform.TransformPosition(OrbitPlanePosition);
}
