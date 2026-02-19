// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CropData.h"
#include "CropBase.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPlantGrowthStageChanged, FIntPoint, Coords, ECropGrowthStage, NewGrowthStage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCropStateChanged, FIntPoint, Coords, FCropStateData, NewCropState);

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
	
	// ==================== 生命周期 ====================
	// 生命周期变化代理
	UPROPERTY(BlueprintAssignable, Category = "Crop|Events")
	FOnPlantGrowthStageChanged OnPlantGrowthStageChanged;

	// 状态变化代理
	UPROPERTY(BlueprintAssignable, Category = "Crop|Events")
	FOnCropStateChanged OnCropStateChanged;
	
	// 初始化农作物
	UFUNCTION(BlueprintCallable, Category = "Crop|Initialization")
	virtual void InitializeCrop(FCropData& InCropData);
	
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
	// 浇水
	UFUNCTION(BlueprintCallable, Category = "Crop|Actions")
	virtual bool WaterCrop(float Amount = 100.0f);
    
	// 施肥
	UFUNCTION(BlueprintCallable, Category = "Crop|Actions")
	virtual bool FertilizeCrop();
    
	// 除虫
	UFUNCTION(BlueprintCallable, Category = "Crop|Actions")
	virtual bool RemovePests();
    
	// 收获
	UFUNCTION(BlueprintCallable, Category = "Crop|Actions")
	virtual void HarvestCrop();

private:
	UPROPERTY()
	FCropData CropData;


};
