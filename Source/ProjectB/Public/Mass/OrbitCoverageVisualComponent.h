// OrbitCoverageVisualComponent.h
// 轨道可视化组件：轨道圆环（引擎网格段拼接）+ 服务范围圆柱（引擎 Cylinder 缩放）+ 覆盖格高亮
// 覆盖模型：平面带（点到轨道平面距离 ≤ 服务半径；判定唯一权威在 SatelliteMassFragments.h）
// 实现说明：零程序化几何——全部使用引擎基础网格（BasicShapes）+ 实例/变换，编辑时只更新 Transform
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Mass/SatelliteManagerComponent.h"   // FRuntimeOrbitData（ComputeCoveredCells 参数需完整类型）
#include "Subsystem/ISMManager.h"             // FGlobalInstanceId / Batch* 实例接口（通用 ISM 管理器）
#include "OrbitCoverageVisualComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class APlanetActor;
class UCellManagerComponent;
class USatelliteManagerComponent;

/**
 * 轨道可视化组件（挂 PlanetActor）
 * - 每轨道：轨道圆环（N 段网格沿切线拼圆）+ 服务范围圆柱（网格缩放；半高 = 服务距离）
 * - 渲染统一经 ISMManager（单桶路径）：颜色经实例 CustomData（RGBA，索引 0-3）传递；
 *   材质取自网格资产默认材质（不再由组件配置）
 * - 覆盖格高亮：复用轮廓高亮层（FHighlightFragment；增删 diff，批量标记）——覆盖的权威表达
 * - 刷新节流：轨道数据变化置脏 → Tick 中按 RebuildInterval 重建（编辑拖动友好）
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class PROJECTB_API UOrbitCoverageVisualComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UOrbitCoverageVisualComponent();

	/** 初始化（PlanetActor 就绪后调用；订阅轨道数据变化） */
	void Initialize(APlanetActor* InPlanetActor);

	/** 可视化总开关（轨道编辑模式进入/退出时切换） */
	UFUNCTION(BlueprintCallable, Category = "OrbitVisual")
	void SetVisualizationEnabled(const bool bEnabled);

	/** 立即刷新全部轨道可视化（轨道环 + 范围圆柱 + 覆盖格高亮） */
	UFUNCTION(BlueprintCallable, Category = "OrbitVisual")
	void RefreshVisualization();

	/** 查询指定格子当前是否在覆盖集（调试/验证用） */
	UFUNCTION(BlueprintCallable, Category = "OrbitVisual")
	bool IsCellCovered(const FIntPoint Coords) const { return CurrentCoveredCells.Contains(Coords); }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 轨道数据变化回调（置脏标记，Tick 中节流重建） */
	UFUNCTION()
	void HandleRuntimeOrbitChanged(FName OrbitName);

	/** 重建单条轨道的可视化（轨道环段 + 范围圆柱变换） */
	void RebuildOrbitVisual(const FName OrbitName);

	/** 重建覆盖格高亮（多轨道并集 → 增删/颜色 diff → fragment 标记；值为该格归属轨道的覆盖色） */
	void RebuildCellHighlight(const TMap<FIntPoint, FLinearColor>& NewCoveredCells);

	/** 计算覆盖格集合（多轨道并集；每格记录归属轨道覆盖色——列表序靠前者优先） */
	void ComputeCoveredCells(const TArray<FRuntimeOrbitData>& Orbits, TMap<FIntPoint, FLinearColor>& OutCells) const;

	/** 移除指定轨道的全部可视化实例（ISM 管理器回收） */
	void RemoveOrbitVisualInstances(const FName OrbitName);

	/** 移除全部轨道的可视化实例（关闭可视化/清理） */
	void RemoveAllVisualInstances();

	// ---- 配置（蓝图/细节面板） ----

	/** 环段网格（细节面板配置，如引擎基础 Cylinder；尺寸自动读取） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual")
	TObjectPtr<UStaticMesh> RingSegmentMesh;

	/** 环段数（越大越圆滑；小值呈虚线感） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual", meta = (DisplayName = "环段数", ClampMin = "8"))
	int32 RingSegmentCount = 96;

	/** 段间隙比例（0 = 实心圆环；0.3 = 虚线感） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual", meta = (DisplayName = "段间隙", ClampMin = "0.0", ClampMax = "0.9"))
	float RingSegmentGap = 0.f;

	/** 环线粗（直径 cm） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual", meta = (DisplayName = "环线粗", ClampMin = "1"))
	float RingLineThickness = 150.f;

	/** 服务范围圆柱网格（细节面板配置，如引擎基础 Cylinder；尺寸自动读取） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual")
	TObjectPtr<UStaticMesh> CylinderMeshAsset;

	/** 圆柱半径兜底系数（圆柱半径 = max(轨道半径, 星球半径 × 该系数)） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual", meta = (DisplayName = "圆柱半径兜底系数", ClampMin = "1.0"))
	float CylinderRadiusScale = 1.1f;

	// 注：实例经 ISMManager 管理；环/圆柱材质为组件级（同一网格共享组件 → 全局统一色）；
	//     多轨道颜色区分体现在覆盖格高亮（每轨道 CoverageColor）。

	/** 轨道环覆盖材质（可选；为空则基于网格默认材质创建动态实例） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual", meta = (DisplayName = "轨道环材质"))
	TObjectPtr<UMaterialInterface> RingMaterial;

	/** 环颜色（写入环材质 BaseColor 参数；全局统一） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual")
	FLinearColor RingColor = FLinearColor(0.2f, 1.0f, 1.0f, 1.0f);

	/** 圆柱覆盖材质（可选；半透明时基于网格默认材质创建动态实例） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual", meta = (DisplayName = "圆柱材质"))
	TObjectPtr<UMaterialInterface> CylinderMaterial;

	/** 圆柱颜色（写入圆柱材质 BaseColor 参数；全局统一） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual")
	FLinearColor CylinderColor = FLinearColor(0.1f, 0.6f, 1.0f, 0.25f);

	/** 是否启用覆盖格高亮 */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual", meta = (DisplayName = "启用覆盖格高亮"))
	bool bEnableCellHighlight = true;

	/** 重建节流间隔（秒；编辑拖动时限制重建频率） */
	UPROPERTY(EditDefaultsOnly, Category = "OrbitVisual", meta = (DisplayName = "重建节流", ClampMin = "0.016"))
	float RebuildInterval = 0.1f;

	// ---- 运行时 ----

	UPROPERTY()
	TObjectPtr<APlanetActor> PlanetActor;

	UPROPERTY()
	TObjectPtr<UCellManagerComponent> CellManager;

	UPROPERTY()
	TObjectPtr<USatelliteManagerComponent> SatelliteManager;

	/** 每轨道的可视化条目（环 = ISM 管理器实例组；圆柱 = 独立静态网格组件） */
	struct FOrbitVisualEntry
	{
		TArray<FGlobalInstanceId> RingInstanceIds;          // 环段实例（每段一个，经 ISMManager）
		int32 RingSegmentCount = 0;                         // 建时的段数（变化则重建实例组）
		TObjectPtr<UStaticMeshComponent> CylinderMesh;      // 圆柱组件（每轨道独立；不占 ISM 实例）
		TObjectPtr<UMaterialInstanceDynamic> CylinderMID;   // 圆柱材质实例（每轨道独立 → 按轨道色）
	};
	TMap<FName, FOrbitVisualEntry> OrbitVisuals;

	/** 环材质实例（从 ISMManager 组件创建；同一网格共享 → 全局统一色） */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMID;

	/** 当前覆盖格集合（坐标 → 归属轨道覆盖色；用于增删/颜色 diff） */
	TMap<FIntPoint, FLinearColor> CurrentCoveredCells;

	/** 待重建脏标记 + 节流计时 */
	bool bPendingRebuild = false;
	float TimeSinceRebuild = 0.f;

	/** 可视化开关 */
	bool bVisualizationEnabled = false;
};
