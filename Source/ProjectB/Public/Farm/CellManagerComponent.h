// CellManagerComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MassEntityTypes.h"
#include "Mass/CellMassFragments.h"
#include "Tools/FarmSaveGame.h"
#include "CellManagerComponent.generated.h"

/**
 * 格子 Slot 状态变更通知（意图委托：调用方表达"哪个 Slot 应启停"，接收方执行）
 * @param Coords    格子坐标
 * @param SlotIndex 0=Plot 地块 / 1=Crop 作物
 * @param bEnabled  目标启停状态
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnCellSlotStateChanged, FIntPoint, int32, bool);

class ABasePlayerController;
class UPlanetConfigDataAsset;
class UFarmlandConfigDataAsset;
class UDataTable;

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
	
	bool GetCellCoordsFromScreenMouse(FIntPoint& OutCellCoords, float& OutWorldDistance) const;

	bool GetCellEntity(FIntPoint Coords, FMassEntityHandle& OutEntity);

	/** 获取指定格子的世界变换（读 FCellStaticFragment.Transform） */
	bool GetCellWorldTransform(FIntPoint Coords, FTransform& OutTransform) const;

	/**
	 * Slot 状态变更通知（意图委托）：耕地/播种等操作后广播，携带目标 Slot 与启停意图
	 * 订阅方（SmartObjectManager）据此 SetSlotEnabled；调用方不读当前状态，规避 Defer 时序
	 */
	FOnCellSlotStateChanged OnCellSlotStateChanged;

	UFUNCTION(BlueprintCallable)
	TArray<FIntPoint> GetAllCoords();

	UFUNCTION(BlueprintCallable)
	void Test();
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

	// 种植作物
	void PlantCrop(FIntPoint Coords, const FString& CropName);

	// 收获作物
	bool HarvestCrop(FIntPoint Coords);

	// 移除作物
	void RemoveCrop(FIntPoint Coords);

	// 作物干掉
	void CropDried(FIntPoint Coords);

	// 移除作物干掉
	void RemoveCropDried(FIntPoint Coords);

	FLinearColor GetOutlineColorForEditMode() const;
	
	UFUNCTION()
	void OnEditModeChanged(EEditMode NewEditMode);
	
	void PrecreateISMComponents();

	// ---- 配置 ----
	UPROPERTY()
	TObjectPtr<UPlanetConfigDataAsset> PlanetConfig;

	UPROPERTY()
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	UPROPERTY()
	TObjectPtr<UDataTable> CropDataTable;

	// ---- Mass 实体列表 ----
	TArray<FMassEntityHandle> CellEntities;

	// 当前高亮坐标
	FIntPoint CurrentOutlineCoords = FIntPoint(-1, -1);

	// 玩家控制器
	UPROPERTY()
	TObjectPtr<ABasePlayerController>  BasePlayerController;

	// 父级Actor
	UPROPERTY()
	TWeakObjectPtr<AActor> OwnerActor;

	FTimerHandle MatureQueueProcessTimer;
	void ProcessMatureQueue();
	void OnAutoHarvest(FIntPoint Coords);
	void OnRemoveCrop(FIntPoint Coords);
	void OnRePlantCrop(FIntPoint Coords);

};