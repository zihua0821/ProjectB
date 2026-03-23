// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PlanetInterface.generated.h"

struct FPlanetInfo;
// This class does not need to be modified.
UINTERFACE(MinimalAPI, BlueprintType)
class UPlanetInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class PROJECTB_API IPlanetInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:

	// 设置星球数据
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Planet")
	void SetPlanetInfo(const FPlanetInfo& InPlanetData);
	virtual void SetPlanetInfo_Implementation(const FPlanetInfo& InPlanetData);
	
	// 获取星球数据
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Planet")
	FPlanetInfo GetPlanetInfo() const;
	virtual FPlanetInfo GetPlanetInfo_Implementation() const;

	// 获取星球数据 (引用)
	virtual FPlanetInfo& GetPlanetInfoRef();

	// 通知数据已改变
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Planet")
	void CallPlanetInfoChanged();
	virtual void CallPlanetInfoChanged_Implementation();

	// 通知旋转更新
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Rotation")
	void UpdateRotation(float NewAngle);
	virtual void UpdateRotation_Implementation(float NewAngle);
};
