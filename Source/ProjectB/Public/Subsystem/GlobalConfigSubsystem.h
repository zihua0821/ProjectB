// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GlobalConfigSubsystem.generated.h"

class UPlanetConfigDataAsset;
class UFarmlandConfigDataAsset;
class USatelliteConfigDataAsset;
/**
 * 
 */
UCLASS()
class PROJECTB_API UGlobalConfigSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 设置配置（由 UCelManagerComponent 初始化时调用） */
	void SetConfigs(UFarmlandConfigDataAsset* InFarmlandConfig, UPlanetConfigDataAsset* InPlanetConfig, UDataTable* InCropTable);

	/** 设置ISM挂载父组件 */
	void SetISMAttachParent(USceneComponent* InParent) { ISMAttachParent = InParent; }

	void SetUniqueMeshes(const TArray<TPair<TObjectPtr<UStaticMesh>, int32>>& InUniqueMeshes) { UniqueMeshes = InUniqueMeshes;}

	/** 获取农场配置 */
	UFarmlandConfigDataAsset* GetFarmlandConfig() const { return FarmlandConfig; }

	/** 设置卫星配置（由 USatelliteManagerComponent 初始化时注入） */
	void SetSatelliteConfig(USatelliteConfigDataAsset* InConfig) { SatelliteConfig = InConfig; }

	/** 获取卫星配置（可能为空——消费方自行兜底默认） */
	USatelliteConfigDataAsset* GetSatelliteConfig() const { return SatelliteConfig; }

	/** 获取星球配置 */
	UPlanetConfigDataAsset* GetPlanetConfig() const { return PlanetConfig; }

	/** 获取作物数据表 */
	UDataTable* GetCropDataTable() const { return CropDataTable; }

	/** 获取ISM挂载父组件 */
	USceneComponent* GetISMAttachParent() const { return ISMAttachParent; }

	TArray<TPair<TObjectPtr<UStaticMesh>, int32>> GetUniqueMeshes() const { return UniqueMeshes; }

	TQueue<FIntPoint, EQueueMode::Mpsc> MatureCropQueue;
private:
	UPROPERTY()
	TObjectPtr<UFarmlandConfigDataAsset> FarmlandConfig;

	UPROPERTY()
	TObjectPtr<UPlanetConfigDataAsset> PlanetConfig;

	UPROPERTY()
	TObjectPtr<USatelliteConfigDataAsset> SatelliteConfig;

	UPROPERTY()
	TObjectPtr<UDataTable> CropDataTable;

	UPROPERTY()
	TObjectPtr<USceneComponent> ISMAttachParent;

	TArray<TPair<TObjectPtr<UStaticMesh>, int32>> UniqueMeshes;

};
