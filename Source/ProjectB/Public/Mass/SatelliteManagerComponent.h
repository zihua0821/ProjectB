// SatelliteManagerComponent.h
// 卫星管理组件：批量创建卫星 Mass 实体（手写路线）+ 轨道数据初始化 + StateTree 挂载
// 资产/参数在蓝图或细节面板配置
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MassEntityTypes.h"
#include "Planet/OrbitManagerComponent.h"   // FOrbitDefinition
#include "SatelliteManagerComponent.generated.h"

class UStateTree;
class APlanetActor;

/**
 * 卫星管理组件
 * 挂载在星球 Actor 上，负责：
 * - 批量创建卫星实体（CreateArchetype + BatchCreateEntities，与格子创建模式一致）
 * - 初始化轨道运动数据（FMassOrbitFragment）
 * - 挂载 StateTree（InstanceFragment + ConstSharedFragment；激活/实例化由引擎 Processor 自动完成）
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class PROJECTB_API USatelliteManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USatelliteManagerComponent();

	/** 初始化：创建卫星实体（需 PlanetConfig 有效；不依赖 OrbitManager 运行时覆盖集） */
	void Initialize(APlanetActor* InPlanetActor);

	/** 获取所有卫星实体 */
	const TArray<FMassEntityHandle>& GetSatelliteEntities() const { return SatelliteEntities; }

private:
	/** 解析目标轨道定义（指定名优先，其次第一个配置，最后兜底默认轨道） */
	bool ResolveOrbitDefinition(FOrbitDefinition& OutDefinition) const;

	// ---- 配置（蓝图/细节面板） ----

	/** StateTree 资产（Mass Schema；为空则只创建实体不挂 StateTree） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite")
	TObjectPtr<UStateTree> StateTreeAsset;

	/** 卫星数量 */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite", meta = (ClampMin = "1", ClampMax = "10000"))
	int32 SatelliteCount = 3;

	/** 角速度（度/秒） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite")
	float DefaultAngularVelocity = 10.0f;

	/** 运行方向：true=顺时针, false=逆时针 */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite")
	bool bDefaultClockwise = false;

	/** 默认轨道名（为空则取配置中第一个轨道） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite")
	FName DefaultOrbitName = NAME_None;

	/** 卫星网格体（渲染用的静态网格；为空则仅创建实体不渲染） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite", meta = (DisplayName = "卫星网格"))
	TObjectPtr<UStaticMesh> SatelliteMesh;

	/** 卫星缩放倍率（应用于网格体） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite", meta = (DisplayName = "卫星缩放", ClampMin = "0.01"))
	float SatelliteScale = 10.0f;

	// ---- 运行时 ----

	UPROPERTY()
	TObjectPtr<APlanetActor> PlanetActor;

	/** 已创建的卫星实体 */
	TArray<FMassEntityHandle> SatelliteEntities;
};
