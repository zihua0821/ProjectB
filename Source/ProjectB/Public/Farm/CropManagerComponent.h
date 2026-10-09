// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Crops/CropData.h"
#include "Tools/FarmSaveGame.h"
#include "CropManagerComponent.generated.h"

class ABasePlayerController;
class UCropBase;
class UInstancedStaticMeshComponent;
class UFarmlandComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCropPlantedOrRemoved, FIntPoint, Coords);

/**
 * 农作物管理组件
 * 负责处理所有与农作物相关的逻辑，与UFarmlandComponent协同工作
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTB_API UCropManagerComponent : public USceneComponent
{
	GENERATED_BODY()
    
public:
    UCropManagerComponent();

    // ===================== 事件 ====================
    // 作物种植或移除
    UPROPERTY(BlueprintAssignable)
    FOnCropPlantedOrRemoved OnCropPlantedOrRemoved;

    // ===================== 公开方法 ====================
    
    // 初始化（在 FarmlandActor 的 PostInitializeComponents 中调用）
    void Initialize(UFarmlandComponent* InFarmlandComponent);

    // 处理左键点击
    UFUNCTION(BlueprintCallable, Category = "Farmland")
    void HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint Coords);

    // 种植作物
    UFUNCTION(BlueprintCallable, Category = "Crop Manager")
    void PlantCrop(FIntPoint Coords, const FString& CropName);

    // 收获作物
    UFUNCTION(BlueprintCallable, Category = "Crop Manager")
    bool HarvestCrop(FIntPoint Coords);

    // 移除作物
    UFUNCTION(BlueprintCallable, Category = "Crop Manager")
    void RemoveCrop(FIntPoint Coords);

    // 作物干掉
    UFUNCTION(BlueprintCallable, Category = "Crop Manager")
    void CropDried(FIntPoint Coords);

    // 移除作物干掉
    UFUNCTION(BlueprintCallable, Category = "Crop Manager")
    void RemoveCropDried(FIntPoint Coords);
    
    // 作物成长阶段改变
    void OnCropGrowthStageChanged(FIntPoint Coords, ECropGrowthStage NewGrowthStage);
    
    // ==================== 接口 ====================
    
    // 设置地块配置数据
    void SetFarmlandConfig(const TObjectPtr<UFarmlandConfigDataAsset>& InFarmlandConfig){FarmlandConfig = InFarmlandConfig;}

    // 设置农作物数据表
    void SetCropDataTable(const TObjectPtr<UDataTable>& InCropDataTable){CropDataTable = InCropDataTable;}

    // 获取地块上的作物
    UFUNCTION(BlueprintPure, Category = "Crop Manager")
    UCropBase* GetCropAt(FIntPoint Coords) const;

    // 获取地块作物信息
    UFUNCTION(BlueprintPure, Category = "Crop Manager")
    FPlotCropInfo GetPlotCropInfo(FIntPoint Coords) const;

    // 获取农作物保存数据
    UFUNCTION(BlueprintCallable, Category = "Farmland")
    TArray<FCropSaveGameData> GetCropSaveGameData();

    // 设置农作物保存数据
    UFUNCTION(BlueprintCallable, Category = "Farmland")
    void SetCropSaveGameData(const TArray<FCropSaveGameData>& InCropSaveGameData){CropSaveGameData = InCropSaveGameData;}

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:

    // 父级Actor
    UPROPERTY()
    TWeakObjectPtr<AActor> OwnerActor;
    
    // 玩家控制器（弱引用：非拥有观察——PC 可能先于组件销毁）
    UPROPERTY()
    TWeakObjectPtr<ABasePlayerController>  BasePlayerController;
    
    // 地块组件（弱引用，避免循环）
    UPROPERTY()
    TWeakObjectPtr<UFarmlandComponent> FarmlandComponent;

    // 作物数据表
    UPROPERTY()
    TObjectPtr<UDataTable> CropDataTable;

    // 地块网格配置
    UPROPERTY()
    TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

    // 地块作物信息
    UPROPERTY()
    TMap<FIntPoint, FPlotCropInfo> PlotCropMap;

    // // 作物渲染组件映射
    // UPROPERTY()
    // TMap<FStaticMeshKey, UInstancedStaticMeshComponent*> CropHISMComponentMap;
    
    // 农作物保存数据
    UPROPERTY()
    TArray<FCropSaveGameData> CropSaveGameData;

    // 初始化农作物
    void InitializeCrops();
    
    // 更新所有作物（由 Tick 驱动）
    void UpdateCrops(float DeltaTime);
    
    // 获取或者创建作物渲染组件
    // UInstancedStaticMeshComponent* GetOrCreateCropHISMComponent(const FPlotCropInfo& CropInfo);

    // 设置作物病害
    void SetCropDisease(FIntPoint Coords,bool bDisease, bool bLeafs = false);

    // 地块状态改变
    UFUNCTION()
    void HandlePlotStateChanged(FIntPoint Coords, FPlotStateData NewPlotState);

    // 地块湿度改变
    UFUNCTION()
    void HandlePlotMoistureChanged(FIntPoint Coords, float NewMoisture);

    // 地块肥力改变
    UFUNCTION()
    void HandlePlotFertilityChanged(FIntPoint Coords, float NewFertility);

    // 作物生长
    UFUNCTION()
    void OnCropGrownThisTime(FIntPoint Coords, float MoistureCost, float FertilityCost) const;

    // 从保存数据初始化作物
    void InitCropsFromSaveGameData();

};