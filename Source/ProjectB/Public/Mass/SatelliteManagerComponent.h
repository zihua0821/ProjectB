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
 * 卫星选点评分权重（EQS 迁移版：状态权重 + 距离权重）
 * 缺口阈值化（缺水/缺肥低于阈值才算缺口），分值越高越优先
 */
USTRUCT(BlueprintType)
struct PROJECTB_API FSatelliteScoringWeights
{
	GENERATED_BODY()

	/** 成熟待收 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "成熟权重"))
	float HarvestWeight = 20.f;

	/** 已收获待重播 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "重播权重"))
	float ReplantWeight = 15.f;

	/** 杂草 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "杂草权重"))
	float WeedWeight = 30.f;

	/** 虫害 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "虫害权重"))
	float PestWeight = 30.f;

	/** 缺水（按 1-水分 连续计分） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "水分权重"))
	float MoistureWeight = 20.f;

	/** 缺肥（按 1-肥力 连续计分） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "肥力权重"))
	float FertilityWeight = 20.f;

	/** 距离权重（与卫星当前角度的角度差，度；越小越好） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "距离权重"))
	float DistanceWeight = 0.5f;

	/** 缺水判定阈值（水分低于此值才算缺口） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "水分阈值", ClampMin = "0.0", ClampMax = "1.0"))
	float MoistureThreshold = 0.7f;

	/** 缺肥判定阈值（肥力低于此值才算缺口） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "肥力阈值", ClampMin = "0.0", ClampMax = "1.0"))
	float FertilityThreshold = 0.7f;

	/** 最低选择分（最高分低于此值时不选目标） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "最低选择分"))
	float MinScoreThreshold = 1.f;

	/** 轨道覆盖半厚系数（1.0 = 旧语义：轨道平面±半格高；增大则更宽容） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "覆盖半厚系数", ClampMin = "0.1"))
	float CoverageHalfThicknessScale = 1.f;
};

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

	/** 活跃农田坐标集合（已耕地+已播种；播种入、移除作物出） */
	const TSet<FIntPoint>& GetActiveFarmlandCoords() const { return ActiveFarmlandCoords; }

	/** 选点评分权重（EQS 迁移版） */
	const FSatelliteScoringWeights& GetScoringWeights() const { return ScoringWeights; }

private:
	/** 解析目标轨道定义（指定名优先，其次第一个配置，最后兜底默认轨道） */
	bool ResolveOrbitDefinition(FOrbitDefinition& OutDefinition) const;

	/** CellManager 意图委托回调：Crop Slot（1）启停 → 维护活跃农田集合 */
	void HandleCellSlotStateChanged(const FIntPoint Coords, const int32 SlotIndex, const bool bEnabled);

	// ---- 选点评分（EQS 迁移） ----

	/** 选点评分权重（蓝图可配） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite|Scoring")
	FSatelliteScoringWeights ScoringWeights;

	/** 活跃农田集合（播种入、移除作物出） */
	TSet<FIntPoint> ActiveFarmlandCoords;

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
