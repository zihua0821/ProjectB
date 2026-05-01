// CellManagerComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MassEntityTypes.h"
#include "Mass/CellMassFragments.h"
#include "Tools/FarmSaveGame.h"
#include "CellManagerComponent.generated.h"

class ABasePlayerController;
class UPlanetConfigDataAsset;
class UFarmlandConfigDataAsset;
class UDataTable;
class UISMPoolManager;

/**
 * 单元格管理中心组件
 * 挂载在星球 Actor 上，负责：
 * - 创建 Mass 实体（世界坐标）
 * - 初始化 ISM 渲染资源（ISMRoot 位于原点，ISM 实例使用世界坐标）
 * - 转发玩家交互（通过 Mass 实体修改数据）
 * - 保存/加载单元格与作物状态
 */
UCLASS(BlueprintType, meta = (BlueprintSpawnableComponent))
class PROJECTB_API UCellManagerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCellManagerComponent();

	/** 初始化配置并创建系统 */
	void Initialize(UPlanetConfigDataAsset* InPlanetConfig, UFarmlandConfigDataAsset* InFarmConfig, UDataTable* InCropTable);

	/** 玩家左键点击处理 */
	UFUNCTION(BlueprintCallable, Category = "Cell")
	void HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint Coords);

	/** 获取所有单元格保存数据 */
	TArray<FFarmLandPlotSaveGameData> GetSaveData() const;

	/** 应用保存数据 */
	void ApplySaveData(const TArray<FFarmLandPlotSaveGameData>& Data);

	// 编辑模式与轮廓
	void UpdateOutline(FIntPoint Coords, bool bEnable);

	/** 通过射线检测获取单元格坐标 */
	bool GetCellCoordsByRaycast(const FVector& RayStart, const FVector& RayEnd, FIntPoint& OutCellCoords, float& OutHitDistance) const;

	bool GetCellEntity(FIntPoint Coords, FMassEntityHandle& OutEntity);

protected:
	virtual void BeginPlay() override;

private:
	/** 批量创建所有单元格 */
	void CreateMassCell();

	// 重置地块
	void ResetLand(FIntPoint Coords);

	// 耕地
	void PlowLand(FIntPoint Coords);
	
	// 移除耕地
	void RemoveLand(FIntPoint Coords);
	
	// 浇水
	void WetLand(FIntPoint Coords);

	// 变干
	void LandDried(FIntPoint Coords);

	// 施肥
	void FertilizeLand(FIntPoint Coords);

	// 清除施肥
	void ClearFertilize(FIntPoint Coords);

	// 生成杂草
	void GenerateWeeds(FIntPoint Coords);

	// 清除杂草
	void ClearWeeds(FIntPoint Coords);

	// 生成虫害
	void GeneratePest(FIntPoint Coords);

	// 清除虫害
	void ClearPest(FIntPoint Coords);

	void UpdateOutlineColor(FIntPoint Coords);
	
	UFUNCTION()
	void OnEditModeChanged(EEditMode NewEditMode);

	// ---- 配置 ----
	UPROPERTY()
	TObjectPtr<UPlanetConfigDataAsset> PlanetConfig;

	UPROPERTY()
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	UPROPERTY()
	TObjectPtr<UDataTable> CropDataTable;

	// ---- ISM 挂载 ----
	/** 用于承载所有 ISM 组件的静态根节点（始终位于世界原点） */
	UPROPERTY()
	TObjectPtr<USceneComponent> ISMRoot;

	// ---- Mass 实体列表 ----
	TArray<FMassEntityHandle> CellEntities;

	// 当前高亮坐标
	FIntPoint CurrentOutlineCoords = FIntPoint(-1, -1);

	// 玩家控制器
	UPROPERTY()
	TObjectPtr<ABasePlayerController>  BasePlayerController;
};