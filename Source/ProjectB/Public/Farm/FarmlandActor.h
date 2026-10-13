// FarmlandActor.h
// 作物压力场景性能测试（ISMManager 分桶验证样例）
// 真实结构复刻：网格体收集自 CropDataTable 的行 × 变体 × 4 阶段（与 CellManagerComponent 的
// UniqueMeshes 逻辑一致）；实例全部经 UISMManager 管理——平面网格（Cols×Cols）映射为球面网格
// （Lat=Lon=Cols）注入 ConfigureSphereBuckets，格子坐标 (row, col) 作为分桶输入（业务寻址语言），
// 桶数随网格规模自适应（70k 实例 → 36 桶）；阶段切换 = BatchRemoveInstances + BatchAddInstances
// （与真实系统 ISMManager 原生增量 Remove+Add 同构），叠加每帧增量变换更新（GetBucketComponent
// 直取桶组件，UpdateInstanceTransform 5 参版本）与 CustomData 更新（UpdateInstanceCustomData），
// 验证需求场景（7万实例 / 106~583 切换每帧 / 60fps）、分桶收益（bConfigureSphereBuckets 对照：
// 开 = 自动分桶 vs 关 = 全部单桶）及超限压力（50万 / 30fps）。
// 运行配置：DebugGame Editor（引擎 Development → 性能数据可信，项目代码可断点）
// 验证结论与实测数据见 docs/渲染体系/ISMManager技术文档.md §11（性能验证结论）
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "Subsystem/ISMManager.h"
#include "Crops/CropData.h"
#include "FarmlandActor.generated.h"

class UDataTable;

UCLASS()
class PROJECTB_API AFarmlandActor : public AActor
{
	GENERATED_BODY()

public:
	AFarmlandActor();

	// ================= 压力场景配置（关卡 / 蓝图可设） =================
	// 作物数据表：与 PlantCrop 同源（FCropTableRow），构建时收集所有行 × 变体 × 阶段的网格体
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress")
	TObjectPtr<UDataTable> CropDataTable;

	// 活跃实例总数：需求 60000~70000；超限压力可设 500000（远期 30fps 档）。
	// 布局为 Cols×Cols 平面网格（Cols = ceil(sqrt(N))），每格 1 实例，网格映射球面分桶输入
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress", meta = (ClampMin = "1000", ClampMax = "1000000"))
	int32 TotalInstances = 70000;

	// 每帧阶段切换数（每次切换 = 1 Remove + 1 Add，模拟成熟/收获阶段流转）：
	// 需求平均档 106 / 极端档 583（泊松峰值 ~640）；随机选择实例，位置不变 → 落回同一空间桶
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress", meta = (ClampMin = "0", ClampMax = "5000"))
	int32 SwitchesPerFrame = 106;

	// 每帧增量变换更新数（模拟生长漂移动画，走 UpdateInstanceTransform 5 参版本）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress", meta = (ClampMin = "0", ClampMax = "10000"))
	int32 TransformUpdatesPerFrame = 100;

	// 每帧 CustomData 更新数（模拟病害/水分呼吸）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress", meta = (ClampMin = "0", ClampMax = "10000"))
	int32 CustomDataUpdatesPerFrame = 100;

	// 实例网格排布间距（厘米）：决定地图边长（MapSize = Cols×Spacing）与桶 Bounds 尺度
	// （36 桶时单桶 Bounds ≈ 45 格 × Spacing，日志核对剔除局部化的依据）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress")
	float Spacing = 100.0f;

	// 随机种子：复现相同布局与切换序列
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress")
	int32 RandomSeed = 12345;

	// ================= 分桶（ISMManager 球面分桶；对照实验：开 = 自动分桶 vs 关 = 全部单桶） =================
	// 注入球面分桶配置（平面网格映射为球面网格 Lat=Lon=Cols）；关 = 不注入 = 全部单桶（分桶收益基线对照）。
	// 注意：注入是全局配置（WorldSubsystem 级），测试地图应只放本 Actor
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress|Bucketing")
	bool bConfigureSphereBuckets = true;

	// 每桶实例数上限（块大小推导参数，与 INSTANCES_PER_BUCKET 同语义；70k 网格 → 36 桶）。
	// 细分挡位参考：1024 → 81 桶 / 512 → 156 桶（块面积校验回退 23×22）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress|Bucketing", meta = (ClampMin = "256", ClampMax = "16384"))
	int32 InstancesPerBucket = 2048;

	// 3 秒后自动把相机移到地图中心俯视（高度 = MapSize×0.55，俯角 50°）：
	// 模拟"同时可见约 1/3"的局部视野，分桶对照实验用（关 = 保持编辑器视角，手动控制）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress|Bucketing")
	bool bCenterCamera = false;

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaSeconds) override;

private:
	// 数据表行缓存（一行 = 一种作物 = N 变体 × 4 阶段网格体）
	struct FCropRowCache
	{
		FName CropID;
		int32 NumCustomDataFloats = 2;
		TArray<FCropMeshVariant> Variants;
	};
	TArray<FCropRowCache> RowCaches;

	// 实例元数据（索引 0..TotalInstances-1；句柄稳定，切换只改 Id 与 Mesh，位置不变）
	struct FStressInstance
	{
		FGlobalInstanceId Id;            // ISMManager 全局句柄（(桶索引, LocalId)，切换后更新为新 Add 返回值）
		UStaticMesh* Mesh = nullptr;     // 当前阶段网格体（Remove/Add 的目标 Mesh）
		int32 RowIndex = INDEX_NONE;     // RowCaches 下标（作物种类）
		int32 VariantIndex = 0;          // 该行的变体下标
		int32 Stage = 0;                 // 阶段：0=Seed 1=Seedling 2=Mature 3=Harvested
		FIntPoint GridCoord = FIntPoint::ZeroValue; // 平面格坐标 (Lat=row, Lon=col) → 球面分桶输入（切换不变 → 落回同一桶）
		FVector BaseLocation = FVector::ZeroVector; // 网格基准位置（变换更新围绕它做小振幅漂移）
	};
	TArray<FStressInstance> Instances;   // 与 TotalInstances 对齐；数据异常跳过的实例句柄无效，Tick 自动跳过

	// ---- 分桶状态（构建时计算） ----
	int32 Cols = 1;                  // 网格列数 = ceil(sqrt(TotalInstances))（行数同；映射 Lat=Lon=Cols）
	float MapSize = 0.0f;            // 地图边长 = Cols × Spacing（相机定位高度基准）
	FTimerHandle CameraTimerHandle;  // 相机定位延时（3 秒）定时器

	// ISMManager（WorldSubsystem，生命周期与 World 一致，安全）
	TObjectPtr<UISMManager> ISMManager = nullptr;

	// 组件创建参数（构建时填充，切换 Add 复用，保证新桶参数一致）
	FISMComponentParams CachedParams;

	// 网格体 → 每实例 CustomData 数量（构建时收集；同网格体跨行取先注册行的值；Tick 合批 Add 统一用）
	TMap<UStaticMesh*, int32> MeshToNumFloats;

	// ---- 统计（每 5 秒窗口输出：均值 = 窗口累加 / 帧数，见 Tick ④） ----
	double Elapsed = 0.0;         // 运行总时长（秒）
	float LastStatTime = 0.0f;    // 上次统计输出时间（秒），与 Elapsed 差 ≥ 5s 触发输出
	int32 FrameCount = 0;         // 当前窗口帧数
	double TickMsAccum = 0.0;     // 窗口内 tick 总耗时（ms）
	double SwitchMsAccum = 0.0;   // 窗口内切换段耗时（ms）
	double TfmMsAccum = 0.0;      // 窗口内变换更新段耗时（ms）
	double CDataMsAccum = 0.0;    // 窗口内 CustomData 更新段耗时（ms）
	double MaxTickMs = 0.0;       // 窗口内单帧 tick 最大值（ms，峰值观察）
	int64 TotalSwitches = 0;      // 累计成功切换次数（每次 = 1 Remove + 1 Add）
	float Phase = 0.0f;           // 运行相位（秒），驱动变换/呼吸动画并错开各实例时序

	// ---- 工具 ----
	static UStaticMesh* GetVariantStageMesh(const FCropMeshVariant& Variant, int32 Stage); // 阶段(0..3) → 网格体（可能为 null）
	int32 PickRandomStage(const FCropRowCache& Row, int32 VariantIndex, int32 ExcludeStage) const; // 抽非当前阶段（仅非空网格体候选）
	void PositionCamera(); // bCenterCamera：3 秒后定位相机到地图中心俯视（高度 = MapSize×0.55）
};
