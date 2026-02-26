// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CropData.h"
#include "CropBase.generated.h"

class UFarmlandComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlantGrowthStageChanged, FIntPoint, Coords, ECropGrowthStage,
                                             NewGrowthStage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCropStateChanged, FIntPoint, Coords, FCropStateData, NewCropState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCropCropQualityChanged, FIntPoint, Coords, ECropQuality, NewCropQuality);

DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnCropGrownThisTime, FIntPoint, Coords, float, MoistureCost, float, FertilityCost);
/**
 * 农作物基类
 * 所有农作物的抽象基类，提供基础功能和接口
 */
UCLASS(Blueprintable)
class PROJECTB_API UCropBase : public UObject
{
	GENERATED_BODY()

public:
	UCropBase();

	// ==================== 生命代理 ====================
	
	// 生命周期变化代理
	UPROPERTY(BlueprintAssignable, Category = "Crop|Events")
	FOnPlantGrowthStageChanged OnPlantGrowthStageChanged;

	// 状态变化代理
	UPROPERTY(BlueprintAssignable, Category = "Crop|Events")
	FOnCropStateChanged OnCropStateChanged;

	// 品质变化代理
	UPROPERTY(BlueprintAssignable, Category = "Crop|Events")
	FOnCropCropQualityChanged OnCropCropQualityChanged;

	FOnCropGrownThisTime OnCropGrownThisTime;
	
	// ==================== 生命周期 ====================
	
	// 初始化农作物
	UFUNCTION(BlueprintCallable, Category = "Crop|Initialization")
	virtual void InitializeCrop(UFarmlandComponent* InComponent, FCropData& InCropData);

	// 更新农作物状态（每帧/每tick调用）
	UFUNCTION(BlueprintCallable, Category = "Crop|Update")
	virtual void UpdateCrop(float DeltaTime);
    
	// 处理离线时间生长
	UFUNCTION(BlueprintCallable, Category = "Crop|Update")
	virtual void ProcessOfflineGrowth(const FTimespan& OfflineDuration);

	UFUNCTION(BlueprintPure, Category = "Crop|Getter")
	FCropData GetCropData() const { return CropData;}
	FCropData& GetCropDataRef() { return CropData;}
	// ==================== 操作接口 ====================

	// 处理作物水份
	UFUNCTION(BlueprintCallable, Category = "Crop|Actions")
	virtual void HandleCropMoisture(float PlotMoisture);

	// 处理作物肥力
	UFUNCTION(BlueprintCallable, Category = "Crop|Actions")
	virtual void HandleCropFertility(float PlotFertility);

	// 收获
	UFUNCTION(BlueprintCallable, Category = "Crop|Actions")
	virtual void HarvestCrop();

	// 处理作物虫害
	UFUNCTION(BlueprintCallable, Category = "Crop|Actions")
	virtual void HandleCropPestInfested(bool bIsPestInfested);

	// 处理作物杂草
	UFUNCTION(BlueprintCallable, Category = "Crop|Actions")
	virtual void HandleCropWeedInfested(bool bIsWeedInfested);

protected:
	virtual void BeginDestroy() override;
private:
	UPROPERTY()
	TWeakObjectPtr<UFarmlandComponent> OwnerComponent;
	
	UPROPERTY()
	FCropData CropData;

	void UpdateCropQuality(float DeltaTime);
	
	UFUNCTION()
	void HandlePlotStateChanged(FIntPoint Coords, FPlotStateData NewPlotState);

};


