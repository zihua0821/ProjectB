// SatelliteManagerComponent.h
// 卫星管理组件：批量创建卫星 Mass 实体（手写路线）+ 轨道数据初始化 + StateTree 挂载
// 资产/参数在蓝图或细节面板配置
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MassEntityTypes.h"
#include "Mass/OrbitPlanningTypes.h"       // 轨道全局规划：快照/计划结构
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
};

/**
 * 运行时轨道数据（可编辑副本）
 * 几何部分【继承项目唯一权威结构 FOrbitDefinition】
 * （OrbitName / Transform / Radius / ServiceDistance）；其余为运行时可编辑的卫星与视觉属性。
 * Initialize 时从 PlanetConfig->OrbitDefinitions 转换；编辑系统修改此数据，
 * ApplyOrbitChangesForOrbit 按轨重建后生效到卫星实体；可视化组件订阅广播实时刷新
 */
USTRUCT(BlueprintType)
struct PROJECTB_API FRuntimeOrbitData : public FOrbitDefinition
{
	GENERATED_BODY()

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

/** 运行时轨道数据变化广播（可视化组件订阅） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRuntimeOrbitChanged, FName, OrbitName);

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

	/** 转场径向速度（只读；T = |R_orb − R_hover| / 此值；离轨/回轨共用） */
	float GetRadialTransferSpeed() const { return RadialTransferSpeed; }

	// ---- 轨道编辑接口（运行时数据编辑 → 可视化实时刷新 → ApplyOrbitChangesForOrbit 按轨重建卫星） ----

	/** 获取运行时轨道数据（可编辑副本） */
	const TArray<FRuntimeOrbitData>& GetRuntimeOrbits() const { return RuntimeOrbits; }

	/**
	 * 更新指定轨道数据（按 OrbitName 定位；广播 OnRuntimeOrbitChanged 供可视化刷新）
	 * 注意：仅更新数据与可视化；卫星运动需调用 ApplyOrbitChangesForOrbit 按轨重建
	 */
	UFUNCTION(BlueprintCallable, Category = "Satellite|Orbit")
	bool UpdateOrbitData(const FName OrbitName, const FRuntimeOrbitData& NewData);

	/** 新建轨道（成功后重建全部卫星） */
	UFUNCTION(BlueprintCallable, Category = "Satellite|Orbit")
	bool AddOrbit(const FRuntimeOrbitData& NewOrbit);

	/** 移除轨道（成功后重建剩余轨道的卫星） */
	UFUNCTION(BlueprintCallable, Category = "Satellite|Orbit")
	void RemoveOrbit(const FName OrbitName);

	/** 按轨重建：只重建指定轨道的卫星与槽位（其他轨道零触碰——编辑交互正式路径） */
	UFUNCTION(BlueprintCallable, Category = "Satellite|Orbit")
	void ApplyOrbitChangesForOrbit(const FName OrbitName);

	/** 轨道数据变化广播（可视化组件订阅） */
	UPROPERTY(BlueprintAssignable, Category = "Satellite|Orbit")
	FOnRuntimeOrbitChanged OnRuntimeOrbitChanged;

	/** 规划层：滚动节拍入口（抄账 → 求解 → 发单）——由 USatelliteOrbitPlanProcessor 在 Mass 帧序内驱动 */
	void TickOrbitPlanning();

	/** 重置全部卫星的 StateTree 时间基准（选定暂停恢复时调用：暂停窗口不计入任务 AdjustedDeltaTime，防"恢复瞬移到目标"） */
	void ResetSatelliteStateTreeTimeBase();

protected:

private:
	/** 构建运行时轨道数据（从 PlanetConfig->OrbitDefinitions 转换；空则兜底默认轨道） */
	void BuildRuntimeOrbits();

	/** 为全部运行时轨道创建卫星（汇总实体与初始变换后统一注册渲染） */
	void CreateAllSatellites();

	/** 为单条轨道创建一组卫星实体（写入 OutEntities/OutTransforms） */
	void CreateSatellitesForOrbit(const FRuntimeOrbitData& Orbit, FMassEntityManager& EntityManager, TArray<FMassEntityHandle>& OutEntities, TArray<FTransform>& OutTransforms);

	/** 注册卫星渲染实例（ISM 单桶路径、一轨一桶：按轨传入逻辑组键）并回写 InstanceId */
	void RegisterRenderInstances(const TArray<FMassEntityHandle>& Entities, const TArray<FTransform>& Transforms, const int32 GroupKey);

	/** 轨道名 → 逻辑组键（轨道序号 + 1；0 = 默认组——未找到轨道时防御返回） */
	int32 GetOrbitGroupKey(const FName OrbitName) const;

	/** 销毁指定轨道的卫星（渲染实例 + 实体；含归还一切认领——其他轨道保留） */
	void DestroySatellitesForOrbit(const FName OrbitName);

	// ---- 轨道全局规划（群体决策；滚动节拍：抄账 → 求解 → 发单） ----

	/** 抄账：采集全部卫星的规划快照（按轨道分组；同一帧一口气读完） */
	void CollectOrbitSnapshots(TArray<FOrbitSnapshot>& OutSnapshots);

	/** 求解（纯函数：只读快照与参数、无状态、确定性——同一快照必得同一计划；每轨独立） */
	static void BuildOrbitPlan(const FOrbitSnapshot& Snapshot, const FOrbitPlanParams& Params, FOrbitPlan& OutPlan);

	/** 发单：认领目标槽并写回句柄（失败/已占位者跳过；下轮快照自然复核） */
	void DispatchOrbitPlan(const FOrbitPlan& Plan);

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
	int32 SatelliteCount = 10;

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

	/** 转场径向速度（cm/s；时长 T = |R_orb − R_hover| / 此值；离轨/回轨共用） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite", meta = (DisplayName = "转场径向速度", ClampMin = "1.0"))
	float RadialTransferSpeed = 2000.0f;

	/** 轨道槽位倍数（槽数 = 卫星数 × 此值；全局决策的候选自由度） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite", meta = (DisplayName = "轨道槽位倍数", ClampMin = "1", ClampMax = "20"))
	int32 OrbitSlotMultiplier = 5;

	/** 规划节拍间隔（秒；滚动决策频率——不等待执行完成，在途按目标槽计） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite", meta = (DisplayName = "规划节拍间隔", ClampMin = "0.1"))
	float OrbitPlanIntervalSeconds = 1.0f;

	/** 等待角上限（度；排座过滤——候选槽需在此角内转到悬停投影；与位移上限统一 45°） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite", meta = (DisplayName = "等待角上限", ClampMin = "1.0", ClampMax = "360.0"))
	float OrbitPlanWaitAngleLimitDeg = 45.0f;

	/** 整理触发线（最小相邻间距 < 均匀间距 × 此比例 才启动均匀化；0.6 = 六成） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite", meta = (DisplayName = "整理触发线", ClampMin = "0.1", ClampMax = "1.0"))
	float OrbitPlanRebalanceRatio = 0.6f;

	/** 均匀化独立节流（秒；每 N 秒一个整理窗口——落座阶段①仍按规划节拍滚动） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite", meta = (DisplayName = "均匀化节流", ClampMin = "0.1"))
	float OrbitRebalanceIntervalSeconds = 5.0f;

	// ---- 运行时 ----

	UPROPERTY()
	TObjectPtr<APlanetActor> PlanetActor;

	/** 已创建的卫星实体（全部轨道汇总） */
	TArray<FMassEntityHandle> SatelliteEntities;

	/** 上次规划时间（秒；节拍节流） */
	double LastOrbitPlanTime = 0.0;

	/** 上次进入均匀化窗口的时间（秒；阶段②独立节流） */
	double LastRebalanceTime = 0.0;

	/** 运行时轨道数据（Initialize 构建；编辑系统修改后 ApplyOrbitChangesForOrbit 按轨生效到卫星） */
	TArray<FRuntimeOrbitData> RuntimeOrbits;
};
