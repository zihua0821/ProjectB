// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectB/Public/TimeSync/TimeSyncComponent.h"
#include "ProjectB/ProjectB.h"


UTimeSyncComponent::UTimeSyncComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UTimeSyncComponent::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		// 服务器：不需要同步，但可以初始化时间偏移为0
		TimeOffset = FTimespan::Zero();
		bInitialized = true;
	}
	else
	{
		// 客户端：延迟1秒后开始同步
		GetWorld()->GetTimerManager().SetTimer(
			SyncTimerHandle,
			this,
			&ThisClass::RequestTimeSync,
			TimeSyncStartSeconds,
			false
		);
	}
	
}

FDateTime UTimeSyncComponent::GetSyncedUTCTime_Implementation() const
{
	if (HasAuthority())
	{
		// 服务器：直接返回当前UTC
		return FDateTime::UtcNow();
	}
    
	if (!bInitialized)
	{
		// 未同步，返回本地UTC时间
		return FDateTime::UtcNow();
	}
    
	// 客户端：本地时间 + 偏移
	return FDateTime::UtcNow() + TimeOffset;
}

double UTimeSyncComponent::GetSyncedUTCTimestamp_Implementation() const
{
	return GetSyncedUTCTime_Implementation().ToUnixTimestampDecimal();
}

FDateTime UTimeSyncComponent::GetSyncedLocalTime_Implementation() const
{
	// 获取同步的UTC时间
	const FDateTime SyncedUtcTime = GetSyncedUTCTime_Implementation();
	
	// 将UTC时间转换为本地时间
	// 在Unreal Engine中，可以通过FDateTime::UtcNow()和FDateTime::Now()的差值来计算本地时区偏移
	const FDateTime CurrentUtc = FDateTime::UtcNow();
	const FDateTime CurrentLocal = FDateTime::Now();
	const FTimespan TimeZoneOffset = CurrentLocal - CurrentUtc;
	
	// 应用时区偏移到同步的UTC时间上得到同步的本地时间
	return SyncedUtcTime + TimeZoneOffset;
}

double UTimeSyncComponent::GetSyncedLocalTimestamp_Implementation() const
{
	return GetSyncedLocalTime_Implementation().ToUnixTimestampDecimal();
}

void UTimeSyncComponent::RequestTimeSync()
{
	if (HasAuthority())
	{
		return; // 服务器不请求
	}
    
	// 记录客户端发送时间
	LastClientSyncTime = FDateTime::UtcNow();
    
	// 发送请求到服务器
	Server_RequestTimeSync(LastClientSyncTime);
}

void UTimeSyncComponent::Server_RequestTimeSync_Implementation(const FDateTime ClientRequestTime)
{
	// 服务器当前UTC时间
	const FDateTime ServerUtcTime = FDateTime::UtcNow();
	// 服务器当前UTC时间 - 客户端发送时间
	const float ClientToServerTime = (ServerUtcTime - ClientRequestTime).GetTotalMilliseconds();
	const FString PlayerName = GetOwner()->GetName();
    UE_LOG(LogServer, Log, TEXT("Client [%s] TimeSynced, ClientToServerTime: [%f] ms"), *PlayerName, ClientToServerTime);
	// 发送给客户端
	Client_ReceiveTimeSync(ServerUtcTime, ClientToServerTime);
}

void UTimeSyncComponent::Client_ReceiveTimeSync_Implementation(const FDateTime ServerUtcTime, const float ClientToServerTime)
{
	ProcessTimeSyncResponse(ServerUtcTime, ClientToServerTime);
}

void UTimeSyncComponent::ProcessTimeSyncResponse(const FDateTime ServerUtcTime, float ClientToServerTime)
{
	// 客户端接收时间
	const FDateTime ClientReceiveTime = FDateTime::UtcNow();
	// 客户端接收时间 - 服务器当前UTC时间
	const float ServerToClientTime = (ClientReceiveTime - ServerUtcTime).GetTotalMilliseconds();
	const FString PlayerName = GetOwner()->GetName();
	UE_LOG(LogClient, Log, TEXT("Client [%s] TimeSynced, ServerToClientTime: [%f] ms"), *PlayerName, ServerToClientTime);
	
	// 计算RTT
	// RTT = (客户端接收时间 - 客户端发送时间) - (服务器发送时间 - 服务器接收时间)
	// 由于服务器处理时间 ≈ 0
	// 简化：RTT ≈ 客户端接收时间 - LastClientSyncTime
	if (LastClientSyncTime.GetTicks() > 0)
	{
		const FTimespan RttTimespan = ClientReceiveTime - LastClientSyncTime;
		CurrentRtt = RttTimespan.GetTotalMilliseconds();
		OnRttUpdated.Broadcast(CurrentRtt);
	}

	// 估算服务器当前时间偏移（在客户端接收时刻）
	// 计算时钟偏移
	if (CalculateSymmetry(ClientToServerTime, ServerToClientTime) > SymmetryThreshold ||
		FMath::Abs(ClientToServerTime - ServerToClientTime) < RttDeltaThreshold)
	{
		// 使用对称平均延迟
		// 计算单向延迟估计（假设对称网络）
		const float OneWayDelay = CurrentRtt / 2.0f;
		// 服务器当前时间 = 服务器发送时的UTC时间 + 单向延迟
		const FDateTime EstimatedServerNow = ServerUtcTime + FTimespan::FromMilliseconds(OneWayDelay);
		TimeOffset = EstimatedServerNow - ClientReceiveTime;
	}
	else
	{
		// 非对称取最小延迟
		const float MinDelay = FMath::Min(ClientToServerTime, ServerToClientTime);
		TimeOffset = FTimespan::FromMilliseconds(MinDelay);
	}
	const FDateTime SyncedUtc = GetSyncedUTCTime_Implementation();
	const FDateTime SyncedLocalUtc = GetSyncedLocalTime_Implementation();

	OnSyncedTimeUpdated.Broadcast(SyncedUtc, SyncedLocalUtc);
	// 标记已初始化
	bInitialized = true;

	// 10秒后再次同步
	GetWorld()->GetTimerManager().SetTimer(
		SyncTimerHandle,
		this,
		&ThisClass::RequestTimeSync,
		TimeSyncIntervalSeconds,
		false
	);
}

bool UTimeSyncComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

float UTimeSyncComponent::CalculateSymmetry(const float ClientToServer, const float ServerToClient)
{
	if (ClientToServer <= 0 || ServerToClient <= 0)
	{
		return 1.0f; // 避免除零错误，假设对称
	}
    
	// 对称度 = min(d1, d2) / max(d1, d2)
	const float MinDelay = FMath::Min(ClientToServer, ServerToClient);
	const float MaxDelay = FMath::Max(ClientToServer, ServerToClient);
    
	return MinDelay / MaxDelay;
}
