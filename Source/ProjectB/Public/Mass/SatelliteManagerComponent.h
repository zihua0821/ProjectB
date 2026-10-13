// SatelliteManagerComponent.h
// 卫星管理组件：卫星实体创建/销毁 + 运行时轨道数据 + 自建槽池 + 全局规划 + 选点扫描缓存 + 编辑接口（数据/实体两层）
// 配置：卫星配置数据资产（SatelliteConfig——空则全走硬编码默认）
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MassEntityTypes.h"
#include "Mass/OrbitPlanningTypes.h"       // 轨道全局规划：快照/计划结构
#include "Mass/OrbitTypes.h"               // FOrbitDefinition（唯一轨道结构）
#include "Planet/SatelliteConfigDataAsset.h" // USatelliteConfigDataAsset / FSatelliteScoringWeights（配置资产）
#include "SatelliteManagerComponent.generated.h"

class UStateTree;
class APlanetActor;

/**
 * 轨道槽认领句柄（自建槽池——取代 FSmartObjectClaimHandle 在轨道侧的使用）
 * Generation 哨兵：0 = 无效；重建该轨时该轨代际 ++ → 该轨旧句柄全部失效（防幽灵认领）
 */
USTRUCT()
struct PROJECTB_API FOrbitSlotHandle
{
	GENERATED_BODY()

	/** 所属轨道名（句柄自带身份——查询不再另传轨道名） */
	UPROPERTY(VisibleAnywhere, Category = "OrbitSlot")
	FName OrbitName = NAME_None;

	/** 槽索引（[0, 槽数)） */
	UPROPERTY(VisibleAnywhere, Category = "OrbitSlot")
	int32 SlotIndex = INDEX_NONE;

	/** 代际（认领时快照；0 = 无效哨兵） */
	UPROPERTY(VisibleAnywhere, Category = "OrbitSlot")
	uint32 Generation = 0;

	bool IsValid() const { return Generation != 0; }

	bool operator==(const FOrbitSlotHandle& Other) const
	{
		return OrbitName == Other.OrbitName && SlotIndex == Other.SlotIndex && Generation == Other.Generation;
	}
	bool operator!=(const FOrbitSlotHandle& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FOrbitSlotHandle& Handle)
	{
		return HashCombine(HashCombine(GetTypeHash(Handle.OrbitName), static_cast<uint32>(Handle.SlotIndex)), Handle.Generation);
	}
};

/**
 * 轨道槽池（自建轻量槽池——替代“每槽一个智能对象实例”）
 * · 单对象管全部轨道：每轨 = 几何五件 + 占用/位置数组；GT 帧序内单线程访问（无需锁）
 * · 重建该轨（RegisterSingleOrbitSlots）= 清空 + 该轨代际 ++ + 重建数组（O(1) 语义）
 * · 认领即置位（TryClaimOrbitSlot）/ 释放即清位（ReleaseOrbitSlot）——句柄带代际防幽灵
 */
UCLASS()
class PROJECTB_API USatelliteSlotPool : public UObject
{
	GENERATED_BODY()

public:
	/** 全量注册（Initialize 路径；逐轨走 RegisterSingleOrbitSlots） */
	void RegisterOrbitSlots(const TArray<FOrbitDefinition>& Orbits, const int32 SlotMultiplier);

	/** 注册单条轨道（重建/新增共用：清空该轨 + 代际 ++ + 重建数组；不影响其他轨道） */
	void RegisterSingleOrbitSlots(const FOrbitDefinition& Orbit, const int32 SlotMultiplier);

	/** 注销单条轨道（删除轨道用） */
	void UnregisterOrbitSlots(const FName OrbitName);

	/** 槽位随相位推进（由 USatelliteSlotSyncProcessor 在 Mass 帧序内驱动：先槽后星；末尾调调试绘制——按轨开关门控） */
	void UpdateOrbitSlots(const float DeltaSeconds);

	/** 槽位球调试绘制（与数据推进拆分；按轨开关——编辑面板勾选，默认关） */
	void DrawOrbitSlotMarkers(UWorld* World) const;

	/** 设置某轨调试可视化开关（编辑面板——运行时状态；重建保留/删轨消失） */
	void SetOrbitSlotMarkersVisible(const FName OrbitName, const bool bVisible);
	void SetOrbitTrajectoryVisible(const FName OrbitName, const bool bVisible);

	/** 某轨调试可视化开关状态（行还原勾选态/绘制门控） */
	bool IsOrbitSlotMarkersVisible(const FName OrbitName) const;
	bool IsOrbitTrajectoryVisible(const FName OrbitName) const;

	/** 槽位数（未注册返回 0） */
	int32 GetOrbitSlotCount(const FName OrbitName) const;

	/** 槽位当前相位（度；= 基准相位 + 360×i/N，随轨道旋转——选槽判据用） */
	bool GetOrbitSlotPhase(const FName OrbitName, const int32 SlotIndex, float& OutPhaseDeg) const;

	/** 认领轨道槽（GT 单线程：查占用 → 置位 → 返回带代际句柄；失败=已被占用返回无效句柄） */
	FOrbitSlotHandle TryClaimOrbitSlot(const FName OrbitName, const int32 SlotIndex);

	/** 释放轨道槽（校验代际——过期句柄静默忽略） */
	void ReleaseOrbitSlot(const FOrbitSlotHandle& Handle);

	/** 按认领句柄查槽位世界位置（校验代际；句柄自带轨道名） */
	bool GetOrbitSlotWorldPositionByClaim(const FOrbitSlotHandle& ClaimHandle, FVector& OutWorldPosition) const;

	/** 按认领句柄查槽位相位（度） */
	bool GetOrbitSlotPhaseByClaim(const FOrbitSlotHandle& ClaimHandle, float& OutPhaseDeg) const;

	/** 按认领句柄反查槽索引（调试/日志可读性用；无效/过期返回 -1） */
	int32 ResolveSlotIndexByClaim(const FOrbitSlotHandle& ClaimHandle) const;

private:
	/** 每轨槽数据：几何五件 + 占用/位置数组（索引对齐） */
	struct FOrbitSlotTrack
	{
		// ---- 轨道几何（槽 i 相位 = CurrentPhaseDeg + 360×i/N） ----
		FTransform OrbitTransform = FTransform::Identity;
		float OrbitRadius = 0.f;
		float AngularVelocity = 0.f;   // 度/秒（与卫星同源）
		bool bClockwise = false;
		float CurrentPhaseDeg = 0.f;   // 槽 0 当前相位（随 ω 推进）

		// ---- 槽数组（索引对齐） ----
		TArray<bool> bClaimed;             // 占用状态（GT 帧序内单线程——无需原子）
		TArray<FVector> SlotWorldPositions; // 推进时更新（查询/渲染/判据同源数据）

		/** 该轨代际（重建时 ++——0 保留为无效哨兵；认领句柄快照此值） */
		uint32 Generation = 0;

		// ---- 调试可视化（编辑面板勾选——运行时状态，重建保留/删轨消失） ----
		bool bDebugDrawSlotMarkers = false;   // 槽位球
		bool bDebugDrawTrajectory = false;    // 卫星轨迹路径点
	};

	/** 轨道名 → 槽数据 */
	TMap<FName, FOrbitSlotTrack> OrbitTracks;
};

/** 运行时轨道数据变化广播（可视化组件订阅） */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRuntimeOrbitChanged, FName, OrbitName);

/**
 * 选点扫描格条目（性能治本——格静态数据缓存）
 * 构建于 Initialize（一次性 O(总格数)）；选点遍历“每轨覆盖索引”（替代全格扫描 GetAllCoords）
 */
struct PROJECTB_API FCellScanEntry
{
	FIntPoint Coord;
	FVector Location;   // 格心世界位置（覆盖判定/方位角/悬停点同源）
};

/**
 * 卫星管理组件
 * 挂载在星球 Actor 上，负责：
 * - 批量创建卫星实体（CreateArchetype + BatchCreateEntities，与格子创建模式一致）
 * - 初始化轨道运动数据（FMassSatelliteOrbitFragment）
 * - 挂载 StateTree（InstanceFragment + ConstSharedFragment；激活/实例化由引擎 Processor 自动完成）
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class PROJECTB_API USatelliteManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USatelliteManagerComponent();

	/** 初始化：创建卫星实体（需 PlanetConfig 有效） */
	void Initialize(APlanetActor* InPlanetActor);

	/** 获取所有卫星实体 */
	const TArray<FMassEntityHandle>& GetSatelliteEntities() const { return SatelliteEntities; }

	/** 选点评分权重（EQS 迁移版——读配置资产；空则内置默认） */
	const FSatelliteScoringWeights& GetScoringWeights() const;

	/** 选点扫描表（格静态条目——性能治本缓存；Initialize 构建） */
	const TArray<FCellScanEntry>& GetCellScanTable() const { return CellScanTable; }

	/** 该轨覆盖带索引（CellScanTable 下标清单——选点前滤缓存；未注册/未建返回 nullptr） */
	const TArray<int32>* FindOrbitCoverageIndices(const FName OrbitName) const;

	/** 转场径向速度（只读；T = |R_orb − R_hover| / 此值；离轨/回轨共用——空则默认 2000） */
	float GetRadialTransferSpeed() const { return SatelliteConfig ? SatelliteConfig->RadialTransferSpeed : 2000.0f; }

	// ---- 任务侧参数（经组件读取——硬编码默认兜底；资产空时全默认） ----

	/** 卫星认领优先级（玩家 High 可抢占） */
	ESmartObjectClaimPriority GetSatelliteClaimPriority() const { return SatelliteConfig ? SatelliteConfig->SatelliteClaimPriority : ESmartObjectClaimPriority::Normal; }

	/** 离轨对齐容差（度；显示位置与提前量点的相位差上限） */
	float GetDepartAlignToleranceDeg() const { return SatelliteConfig ? SatelliteConfig->DepartAlignToleranceDeg : 1.5f; }

	/** 选点节流间隔（秒；仅巡航态生效） */
	float GetSelectIntervalSeconds() const { return SatelliteConfig ? SatelliteConfig->SelectIntervalSeconds : 0.2f; }

	/** 回轨对齐容差（度；槽位接近悬停点投影的容差） */
	float GetTakeoffAlignToleranceDeg() const { return SatelliteConfig ? SatelliteConfig->TakeoffAlignToleranceDeg : 3.0f; }

	/** 漂移最短时长（秒） */
	float GetDriftMinDurationSeconds() const { return SatelliteConfig ? SatelliteConfig->DriftMinDurationSeconds : 2.0f; }

	/** 漂移最长时长（秒） */
	float GetDriftMaxDurationSeconds() const { return SatelliteConfig ? SatelliteConfig->DriftMaxDurationSeconds : 6.0f; }

	/** 作业时长缺省值（秒；认领句柄/行为定义缺失时使用） */
	float GetDefaultActionDuration() const { return SatelliteConfig ? SatelliteConfig->DefaultActionDuration : 2.0f; }

	/** 悬停高度（cm；离轨作业悬停点距球面距离——配置资产） */
	float GetHoverHeight() const { return SatelliteConfig ? SatelliteConfig->HoverHeight : 1000.0f; }

	// ---- 轨道编辑接口（数据层：运行时数据更新 → 可视化实时刷新；实体层：RebuildOrbitEntities 按轨重建） ----

	/** 获取运行时轨道数据（可编辑副本） */
	const TArray<FOrbitDefinition>& GetRuntimeOrbits() const { return RuntimeOrbits; }

	/**
	 * 【数据层】更新指定轨道数据（按 OrbitName 定位；广播 OnRuntimeOrbitChanged 供可视化刷新）
	 * 注意：仅更新数据与可视化（不触碰实体）；实体重建需调用 RebuildOrbitEntities
	 */
	UFUNCTION(BlueprintCallable, Category = "Satellite|Orbit")
	bool UpdateOrbitData(const FName OrbitName, const FOrbitDefinition& NewData);

	/** 新建轨道（成功后重建全部卫星） */
	UFUNCTION(BlueprintCallable, Category = "Satellite|Orbit")
	bool AddOrbit(const FOrbitDefinition& NewOrbit);

	/** 移除轨道（成功后重建剩余轨道的卫星） */
	UFUNCTION(BlueprintCallable, Category = "Satellite|Orbit")
	void RemoveOrbit(const FName OrbitName);

	/** 【实体层】重建轨道实体（该轨下的全部运行时对象：槽位/卫星/渲染实例/选点缓存）
	 *  注：“实体”为广义所指，非 Mass Entity 单指；其他轨道零触碰——编辑交互正式路径 */
	UFUNCTION(BlueprintCallable, Category = "Satellite|Orbit")
	void RebuildOrbitEntities(const FName OrbitName);

	/** 轨道数据变化广播（可视化组件订阅） */
	UPROPERTY(BlueprintAssignable, Category = "Satellite|Orbit")
	FOnRuntimeOrbitChanged OnRuntimeOrbitChanged;

	/** 规划层：滚动节拍入口（抄账 → 求解 → 发单）——由 USatelliteOrbitPlanProcessor 在 Mass 帧序内驱动 */
	void TickOrbitPlanning();

	/** 重置全部卫星的 StateTree 时间基准（选定暂停恢复时调用：暂停窗口不计入任务 AdjustedDeltaTime，防"恢复瞬移到目标"） */
	void ResetSatelliteStateTreeTimeBase();

	// ---- 轨道槽池（自建：智能对象实例 → 轻量槽池；对外转发——内部方法由 Manager 直调） ----

	/** 槽位随相位推进（帧序驱动转发） */
	void UpdateOrbitSlots(const float DeltaSeconds);

	/** 释放轨道槽（校验代际——过期句柄静默忽略） */
	void ReleaseOrbitSlot(const FOrbitSlotHandle& Handle);

	/** 按认领句柄查槽位世界位置（渲染取位/任务判据转发） */
	bool GetOrbitSlotWorldPositionByClaim(const FOrbitSlotHandle& ClaimHandle, FVector& OutWorldPosition) const;

	/** 按认领句柄查槽位相位（度） */
	bool GetOrbitSlotPhaseByClaim(const FOrbitSlotHandle& ClaimHandle, float& OutPhaseDeg) const;

	// ---- 轨道调试可视化（编辑面板开关——每轨：槽位球 / 卫星轨迹） ----

	/** 槽位球开关（面板勾选 → 池绘制门控） */
	void SetOrbitSlotMarkersVisible(const FName OrbitName, const bool bVisible);
	bool IsOrbitSlotMarkersVisible(const FName OrbitName) const;

	/** 卫星轨迹路径点开关（面板勾选 → 任务绘制门控） */
	void SetOrbitTrajectoryVisible(const FName OrbitName, const bool bVisible);
	bool IsOrbitTrajectoryVisible(const FName OrbitName) const;

protected:

private:
	/** 构建运行时轨道数据（不使用资产轨道定义——直接构造默认单轨；参数读卫星配置） */
	void BuildRuntimeOrbits();

	/** 构建选点扫描表（Initialize——O(总格数) 一次性；格静态数据） */
	void BuildCellScanTable();

	/** 重算该轨覆盖索引（Initialize/轨道重建——O(总格数) 一次性；与选点原硬过滤同源公式） */
	void RebuildOrbitCoverageIndices(const FOrbitDefinition& Orbit);

	/** 为全部运行时轨道创建卫星（汇总实体与初始变换后统一注册渲染） */
	void CreateAllSatellites();

	/** 为单条轨道创建一组卫星实体（写入 OutEntities/OutTransforms） */
	void CreateSatellitesForOrbit(const FOrbitDefinition& Orbit, FMassEntityManager& EntityManager, TArray<FMassEntityHandle>& OutEntities, TArray<FTransform>& OutTransforms);

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

	// ---- 配置（配置资产化——唯一配置源；空则全走硬编码默认） ----

	/** 卫星配置资产（迁移自原组件字段：卫星本体/轨道默认/转场/规划/评分/任务） */
	UPROPERTY(EditDefaultsOnly, Category = "Satellite")
	TObjectPtr<USatelliteConfigDataAsset> SatelliteConfig;

	// ---- 运行时 ----

	UPROPERTY()
	TWeakObjectPtr<APlanetActor> PlanetActor;

	/** 轨道槽池（自建——替代智能对象实例；Initialize 创建） */
	UPROPERTY()
	TObjectPtr<USatelliteSlotPool> OrbitSlotPool;

	/** 已创建的卫星实体（全部轨道汇总） */
	TArray<FMassEntityHandle> SatelliteEntities;

	/** 上次规划时间（秒；节拍节流） */
	double LastOrbitPlanTime = 0.0;

	/** 上次进入均匀化窗口的时间（秒；阶段②独立节流） */
	double LastRebalanceTime = 0.0;

	/** 运行时轨道数据（Initialize 构建；编辑系统修改后 RebuildOrbitEntities 按轨生效到卫星） */
	TArray<FOrbitDefinition> RuntimeOrbits;

	/** 选点扫描表（格静态条目——坐标+格心位置；性能治本缓存） */
	TArray<FCellScanEntry> CellScanTable;

	/** 轨道名 → 覆盖带索引（CellScanTable 下标；Initialize/轨道重建时重算） */
	TMap<FName, TArray<int32>> OrbitCoverageIndices;
};
