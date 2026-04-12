// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OrbitManagerComponent.generated.h"

class UPlanetConfigDataAsset;
class UFarmlandComponent;

USTRUCT(BlueprintType)
struct FOrbitData
{
	GENERATED_BODY()
	
	// 轨道变换
	// - Rotation: 定义轨道平面方向和相位零点
	//   - Roll (X):  轨道方位
	//   - Pitch (Y): 轨道倾角 
	//   - Yaw (Z):   相位零点
	// - Location: 轨道中心点
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	FTransform Transform = FTransform::Identity;

	// 轨道半径 (cm)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	float Radius = 30000.f;

	// 轨道覆盖的地块坐标列表
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Orbit")
	TMap<FIntPoint, float> CoveredCoordsMap;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTB_API UOrbitManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOrbitManagerComponent();

	// 设置地块渲染组件
	void SetCellManager(UFarmlandComponent* InCellManager);

	// 设置星球配置数据
	void SetPlanetConfig(const TObjectPtr<UPlanetConfigDataAsset>& InPlanetConfig);
	
	// 添加轨道
	UFUNCTION(BlueprintCallable, Category = "Orbit Management")
	void AddOrbit(FName OrbitName, const FOrbitData& Definition);

	// 移除轨道
	UFUNCTION(BlueprintCallable, Category = "Orbit Management")
	void RemoveOrbit(FName OrbitName);

	// 获取轨道定义
	UFUNCTION(BlueprintPure, Category = "Orbit Management")
	FOrbitData GetOrbitData(FName OrbitName) const;

	// 检查轨道是否存在
	UFUNCTION(BlueprintPure, Category = "Orbit Management")
	bool HasOrbit(FName OrbitName) const;

	// 获取所有轨道名称
	UFUNCTION(BlueprintPure, Category = "Orbit Management")
	TArray<FName> GetAllOrbitNames() const;

protected:
	virtual void BeginPlay() override;

private:
	void UpdateOrbitCoveredCoords(FName OrbitName);
	void InitOrbit();

	UPROPERTY()
	TMap<FName, FOrbitData> Orbits;

	UPROPERTY()
	TWeakObjectPtr<UFarmlandComponent> CellManager;

	// 星球配置
	UPROPERTY()
	TWeakObjectPtr<UPlanetConfigDataAsset> PlanetConfig;
};


