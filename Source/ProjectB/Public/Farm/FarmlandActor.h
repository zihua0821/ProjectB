// FarmlandActor.h
// 作物压力场景性能测试
// 真实结构复刻：网格体收集自 CropDataTable 的行 × 变体 × 4 阶段（与 CellManagerComponent 的
// UniqueMeshes 逻辑一致），组件 = 唯一网格体 × 空间区块（BucketsPerAxis 分桶，模拟真实系统分桶
// 让视锥/距离剔除生效）；阶段切换 = RemoveInstancesById + AddInstanceById（与真实系统
// ISMManager 原生增量 Remove+Add 同构），叠加每帧增量变换更新与 CustomData 更新，验证需求场景
// （7万实例 / 106~583 切换每帧 / 60fps）、分桶对性能的提升（BPA=1 vs BPA=N 对照）及超限压力（50万 / 30fps）。
// 运行配置：DebugGame Editor（引擎 Development → 性能数据可信，项目代码可断点）
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/TimerHandle.h"
#include "Crops/CropData.h"
#include "FarmlandActor.generated.h"

class UDataTable;
class UInstancedStaticMeshComponent;

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

	// 活跃实例总数：需求 60000~70000；超限压力可设 500000（远期 30fps 档）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress", meta = (ClampMin = "1000", ClampMax = "1000000"))
	int32 TotalInstances = 70000;

	// 每帧阶段切换数（每次切换 = 1 Remove + 1 Add）：需求平均档 106 / 极端档 583（泊松峰值 ~640）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress", meta = (ClampMin = "0", ClampMax = "5000"))
	int32 SwitchesPerFrame = 106;

	// 每帧增量变换更新数（模拟生长漂移动画，走 UpdateInstanceTransform 5 参版本）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress", meta = (ClampMin = "0", ClampMax = "10000"))
	int32 TransformUpdatesPerFrame = 100;

	// 每帧 CustomData 更新数（模拟病害/水分呼吸）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress", meta = (ClampMin = "0", ClampMax = "10000"))
	int32 CustomDataUpdatesPerFrame = 100;

	// 实例网格排布间距（厘米）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress")
	float Spacing = 100.0f;

	// 随机种子：复现相同布局与切换序列
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress")
	int32 RandomSeed = 12345;

	// ================= 分桶模拟（对照实验：1 = 不分桶） =================
	// 每边空间区块数：N → 地图切 N×N 区块，组件 = 网格体 × 区块（桶包围盒小 → 视锥/距离剔除生效）。
	// 对照实验：BPA=1（全地图一个包围盒，剔除失效）vs BPA=4~8（相机照局部 → 只画可见区块）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CropStress|Bucketing", meta = (ClampMin = "1", ClampMax = "16"))
	int32 BucketsPerAxis = 1;

	// 3 秒后自动把相机移到地图中心俯视（模拟"同时可见约 1/3"的局部视野，方便分桶对照）
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

	// 每个 唯一网格体 × 空间区块 对应一个 ISM 组件（组件 Outer=this，RegisterComponent 后被 Actor 强引用，GC 安全）
	struct FStressComponent
	{
		TObjectPtr<UInstancedStaticMeshComponent> Comp = nullptr;
		UStaticMesh* Mesh = nullptr;
		int32 NumCustomDataFloats = 0;
		TArray<FPrimitiveInstanceId> InstanceIds; // 与引擎数组同步（RemoveAtSwap 语义）
		TArray<FVector> BaseLocations;
	};
	TArray<FStressComponent> Components;

	// 实例元数据（索引 0..TotalInstances-1；句柄稳定，RemoveAtSwap 只改映射不改 Id）
	struct FStressInstance
	{
		FPrimitiveInstanceId Id;
		int32 CompIndex = INDEX_NONE;
		int32 RowIndex = INDEX_NONE;
		int32 VariantIndex = 0;
		int32 Stage = 0; // 0=Seed 1=Seedling 2=Mature 3=Harvested
		FVector BaseLocation = FVector::ZeroVector;
	};
	TArray<FStressInstance> Instances;

	// 网格体 → 全局网格体索引（组件索引 = MeshGlobalIdx * NumBuckets + BucketIdx）
	TMap<UStaticMesh*, int32> MeshToGlobalIdx;

	// 分桶状态（构建时计算）
	int32 NumBuckets = 1;
	int32 Cols = 1;
	float MapSize = 0.0f; // 地图边长（相机定位用）
	FTimerHandle CameraTimerHandle;

	// 统计
	double Elapsed = 0.0;
	float LastStatTime = 0.0f;
	int32 FrameCount = 0;
	double TickMsAccum = 0.0;
	double SwitchMsAccum = 0.0;
	double TfmMsAccum = 0.0;
	double CDataMsAccum = 0.0;
	double MaxTickMs = 0.0;
	int64 TotalSwitches = 0;
	float Phase = 0.0f;

	// 工具
	static UStaticMesh* GetVariantStageMesh(const FCropMeshVariant& Variant, int32 Stage);
	int32 PickRandomStage(const FCropRowCache& Row, int32 VariantIndex, int32 ExcludeStage) const;
	void PositionCamera(); // bCenterCamera：3 秒后定位相机到地图中心俯视
};
