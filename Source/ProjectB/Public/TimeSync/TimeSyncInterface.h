// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TimeSyncInterface.generated.h"

// This class does not need to be modified.
UINTERFACE()
class UTimeSyncInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class PROJECTB_API ITimeSyncInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	// ==================== 时间获取接口 ====================
    
	// 获取同步的UTC时间
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Time Sync")
	FDateTime GetSyncedUTCTime() const;
    
	// 获取同步的UTC时间戳（毫秒）
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Time Sync")
	int64 GetSyncedUTCTimestamp() const;
	
	// ==================== 网络状态接口 ====================
    
	// 获取当前RTT（毫秒）
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Time Sync")
	float GetCurrentRTT() const;
};
