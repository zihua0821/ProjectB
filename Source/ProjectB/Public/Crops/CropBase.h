// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CropData.h"
#include "CropBase.generated.h"


/**
 * 农作物基类
 * 所有农作物的抽象基类，提供基础功能和接口
 */
UCLASS()
class PROJECTB_API UCropBase : public UObject
{
	GENERATED_BODY()

public:
	UCropBase();
	
	// ==================== 生命周期 ====================
	// 初始化农作物
	UFUNCTION(BlueprintCallable, Category = "Crop|Initialization")
	virtual void InitializeCrop(const FCropDataRow& Data, const FDateTime& InPlantTime);
	
	// 更新农作物状态（每帧/每tick调用）
	UFUNCTION(BlueprintCallable, Category = "Crop|Update")
	virtual void UpdateCrop(float DeltaTime);
    
	// 处理离线时间生长
	UFUNCTION(BlueprintCallable, Category = "Crop|Update")
	virtual void ProcessOfflineGrowth(const FTimespan& OfflineDuration);

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
};
