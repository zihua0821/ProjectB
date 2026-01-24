// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TimeSyncInterface.h"
#include "Components/ActorComponent.h"
#include "TimeSyncComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTB_API UTimeSyncComponent : public UActorComponent, public ITimeSyncInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UTimeSyncComponent();

	// ==================== UActorComponent接口 ====================
	virtual void BeginPlay() override;
	// ==================== ITimeSyncInterface接口实现 ====================
	// 获取同步的UTC时间
	virtual FDateTime GetSyncedUTCTime_Implementation() const override;
	// 获取同步的UTC时间戳（毫秒）
	virtual int64 GetSyncedUTCTimestamp_Implementation() const override;
	// 获取当前RTT（毫秒）
	virtual float GetCurrentRTT_Implementation() const override
	{
		return CurrentRtt;
	}

	// =================== 时间同步参数 ====================
	// 时间同步开始时间（秒）
	UPROPERTY(EditDefaultsOnly, Category = "Time Sync")
	float TimeSyncStartSeconds = 1.0f;
	// 时间同步间隔时间（秒）
	UPROPERTY(EditDefaultsOnly, Category = "Time Sync")
	float TimeSyncIntervalSeconds = 10.0f;
	// 对称度阈值
	UPROPERTY(EditDefaultsOnly, Category = "Time Sync")
	float SymmetryThreshold = 0.8f;
	// 往返时间差阈值（秒）
	UPROPERTY(EditDefaultsOnly, Category = "Time Sync")
	float RttDeltaThreshold = 20.f;

private:
	// RPC：客户端请求时间同步
	UFUNCTION(Server, Reliable)
	void Server_RequestTimeSync(FDateTime ClientRequestTime);
    
	// RPC：服务器响应时间同步
	UFUNCTION(Client, Reliable)
	void Client_ReceiveTimeSync(FDateTime ServerUtcTime, float ClientToServerTime);
    
	// 处理服务器时间响应
	void ProcessTimeSyncResponse(FDateTime ServerUtcTime, float ClientToServerTime);
    
	// 发起时间同步请求
	void RequestTimeSync();
    
	// 组件拥有者是否有服务器权限
	bool HasAuthority() const;

	// 计算网络对称度
	static float CalculateSymmetry(float ClientToServer, float ServerToClient);
	
private:
	// 当前RTT（毫秒）
	float CurrentRtt = 0.0f;
    
	// 时钟偏移（服务器UTC - 本地UTC）
	FTimespan TimeOffset;
    
	// 最后同步的客户端时间
	FDateTime LastClientSyncTime;
    
	// 定时器句柄
	FTimerHandle SyncTimerHandle;
    
	// 是否已初始化
	bool bInitialized = false;
};


