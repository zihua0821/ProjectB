// Fill out your copyright notice in the Description page of Project Settings.


#include "ProjectB/Public/Player/BasePlayerController.h"

#include "Crops/CropBase.h"
#include "Farm/CellManagerComponent.h"
#include "Farm/FarmlandActor.h"
#include "Farm/FarmlandComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Mass/CellMassFragments.h"           // FHighlightFragment（悬停高亮同步清除）
#include "UMG/OrbitEditorWidget.h"           // 轨道编辑界面（Slate 面板 + UMG 包装一体）
#include "Engine/Engine.h"                    // GEngine->GameViewport（Slate 面板挂载）
#include "Engine/GameViewportClient.h"        // AddViewportWidgetContent
#include "Mass/OrbitCoverageVisualComponent.h"
#include "Mass/SmartObjectManagerComponent.h"
#include "MassEntitySubsystem.h"
#include "Planet/PlanetActor.h"
#include "ProjectB/Public/TimeSync/TimeSyncComponent.h"
#include "Simulation/SimulationControlSubsystem.h"   // 选定暂停（编辑模式进/出）

ABasePlayerController::ABasePlayerController()
{
	TimeSyncComponent = CreateDefaultSubobject<UTimeSyncComponent>(TEXT("TimeSyncComponent"));
}

void ABasePlayerController::SetEditMode(const EEditMode InEditMode)
{
	EditMode = InEditMode;
	OnEditModeChanged.Broadcast(EditMode);
}

void ABasePlayerController::BeginPlay()
{
	Super::BeginPlay();

	SetShowMouseCursor(true);
	FInputModeGameAndUI InputMode;
	// 关键：必须设为 true，否则点击 UI 仍会穿透到场景
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	
	// 获取GameInstance并尝试加载存档
	GameInstance = Cast<UProjectBGameInstance>(GetGameInstance());
	if (GameInstance)
	{
		GameInstance->LoadGame();
	}
}

void ABasePlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// double StartTime = FPlatformTime::Seconds();
	UpdateHoveredPlot();
	// UE_LOG(LogTemp, Warning, TEXT("UpdateHoveredPlot took: %.3f ms"), (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

void ABasePlayerController::OnLeftMouseClick()
{
	// 执行射线检测
	FVector HitLocation;
	APlanetActor* HitPlanet = nullptr;
	FIntPoint CellCoords;
	if (TraceForFarmlandAndCell(HitLocation, HitPlanet, CellCoords))
	{
		ExecuteEditModeWithClaim(HitPlanet, CellCoords);
	}
}

void ABasePlayerController::ExecuteEditModeWithClaim(APlanetActor* Planet, const FIntPoint CellCoords)
{
	if (!Planet)
	{
		return;
	}

	USmartObjectManagerComponent* SmartObjectManager = Planet->GetSmartObjectManagerComponent();
	const int32 SlotIndex = USmartObjectManagerComponent::GetSlotIndexForEditMode(EditMode);

	// 无智能对象系统 / 操作不适用认领（None/Count）：直接执行
	if (!SmartObjectManager || SlotIndex < 0)
	{
		Planet->HandleLeftMouseClicked(EditMode, CellCoords);
		return;
	}

	// 目标 Slot 未启用（对象不存在，如未耕地/未播种）：无竞争对象，直接执行创建类操作
	if (!SmartObjectManager->IsSlotEnabled(CellCoords, SlotIndex))
	{
		Planet->HandleLeftMouseClicked(EditMode, CellCoords);
		return;
	}

	// 已启用：走认领协议（与卫星共享认领/占用；High 优先级可抢占卫星的 Claimed）
	const FSmartObjectClaimHandle ClaimHandle = SmartObjectManager->TryClaimSlot(
		CellCoords, SlotIndex, ESmartObjectClaimPriority::High);
	if (!ClaimHandle.IsValid())
	{
		// 被同级或更高优先级占用：拒绝本次操作
		UE_LOG(LogTemp, Warning, TEXT("[Player] Edit operation rejected: slot occupied at (%d, %d)"), CellCoords.X, CellCoords.Y);
		return;
	}

	// 执行操作（同帧瞬时完成）→ 释放认领
	Planet->HandleLeftMouseClicked(EditMode, CellCoords);
	SmartObjectManager->ReleaseClaim(ClaimHandle);
}

bool ABasePlayerController::TraceForFarmlandAndCell(FVector& OutHitLocation, APlanetActor*& OutPlanetActor,
	FIntPoint& OutCellCoords)
{
	// 获取鼠标位置
    float MouseX, MouseY;
    if (GetMousePosition(MouseX, MouseY))
    {
        // 将鼠标位置转换为世界空间射线
        FVector WorldLocation, WorldDirection;
        if (DeprojectScreenPositionToWorld(MouseX, MouseY, WorldLocation, WorldDirection))
        {
            // 计算射线终点
            float TraceDistance = 10000.0f; // 10米
            FVector TraceStart = WorldLocation;
            FVector TraceEnd = TraceStart + WorldDirection * TraceDistance;
            
            // 执行射线检测
            FHitResult HitResult;
        	
        	// 指定只检测 WorldDynamic 类型
        	FCollisionObjectQueryParams ObjectQueryParams;
        	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);
 
        	// 设置参数
        	FCollisionQueryParams QueryParams;
        	QueryParams.bTraceComplex = false;
        	QueryParams.bReturnPhysicalMaterial = false;
        	// 忽略玩家自身
        	if (GetPawn())
        	{
        		QueryParams.AddIgnoredActor(GetPawn());
        	}
        
            if (GetWorld()->LineTraceSingleByObjectType(HitResult, TraceStart, TraceEnd, ObjectQueryParams, QueryParams))
            {
                OutHitLocation = HitResult.Location;
                // 检查是否击中了FarmlandActor或其子组件
                AActor* HitActor = HitResult.GetActor();
                if (HitActor)
                {
                    // 检查是否是PlanetActor
                    OutPlanetActor = Cast<APlanetActor>(HitActor);
                    
                    // 如果不是直接击中PlanetActor，检查是否是它的组件
                    if (!OutPlanetActor)
                    {
                        OutPlanetActor = Cast<APlanetActor>(HitActor->GetOwner());
                    }
                    
                    // 如果找到了FarmlandActor，计算单元格坐标
                    if (OutPlanetActor)
                    {
                    	if (const UCellManagerComponent* CellManagerComponent = OutPlanetActor->GetCellManagerComponent())
                    	{
                    		float HitDistance;
                    		if (CellManagerComponent->GetCellCoordsFromScreenMouse(OutCellCoords, HitDistance))
                    		{
                    			return true;
                    		}
                    	}
                    }
                }
            }
        }
    }
    
    return false;
}

void ABasePlayerController::UpdateHoveredPlot()
{
	// 轨道编辑模式：悬停高亮与覆盖格高亮互斥（不触发悬停）
	if (bOrbitEditMode)
	{
		return;
	}

	// 执行射线检测
	FVector HitLocation;
	APlanetActor* HitPlanet = nullptr;
	FIntPoint CellCoords = FIntPoint(-1, -1);

	if (TraceForFarmlandAndCell(HitLocation, HitPlanet, CellCoords))
	{
		// DrawDebugSphere(GetWorld(), HitLocation, 5.f, 12, FColor::Red, false, 3.f);
		if (PreHoveredPlotCoords == CellCoords)
		{
			return;
		}
		OnHoveredPlot.Broadcast(HitPlanet, CellCoords);
		if (PreHoveredPlanetActor && PreHoveredPlanetActor != HitPlanet)
		{
			PreHoveredPlanetActor->OutlinePlot(PreHoveredPlotCoords, false);
		}
		else
		{
			HitPlanet->OutlinePlot(PreHoveredPlotCoords, false);
		}
		HitPlanet->OutlinePlot(CellCoords, true);
		PreHoveredPlotCoords = CellCoords;
		PreHoveredPlanetActor = HitPlanet;
	}
	else
	{
		OnHoveredPlot.Broadcast(nullptr, FIntPoint(-1, -1));
		if (PreHoveredPlanetActor)
		{
			PreHoveredPlanetActor->OutlinePlot(PreHoveredPlotCoords, false);
			PreHoveredPlotCoords = FIntPoint(-1, -1);
			PreHoveredPlanetActor = nullptr;
		}
	}
}

void ABasePlayerController::EnterOrbitEditMode()
{
	if (bOrbitEditMode)
	{
		return;
	}
	bOrbitEditMode = true;

	// 清除当前悬停高亮（UpdateOutline 已统一为同步直写——与覆盖标记同路径，无时序竞态）
	if (PreHoveredPlanetActor && PreHoveredPlotCoords != FIntPoint(-1, -1))
	{
		PreHoveredPlanetActor->OutlinePlot(PreHoveredPlotCoords, false);
		PreHoveredPlotCoords = FIntPoint(-1, -1);
		PreHoveredPlanetActor = nullptr;
	}

	// 开启轨道覆盖可视化（轨道圆环 + 服务范围圆柱 + 覆盖格高亮）
	if (APlanetActor* Planet = Cast<APlanetActor>(UGameplayStatics::GetActorOfClass(GetWorld(), APlanetActor::StaticClass())))
	{
		if (UOrbitCoverageVisualComponent* Visual = Planet->GetOrbitCoverageVisualComponent())
		{
			Visual->SetVisualizationEnabled(true);
		}
	}

	// 选定暂停：冻结"世界演化"链（白名单自检生效）；玩家自由飞行/编辑可视化不受影响
	if (UWorld* World = GetWorld())
	{
		if (USimulationControlSubsystem* SimControl = World->GetSubsystem<USimulationControlSubsystem>())
		{
			SimControl->SetSimulationPaused(true);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Player] Orbit edit mode ENTERED"));
}

void ABasePlayerController::ExitOrbitEditMode()
{
	if (!bOrbitEditMode)
	{
		return;
	}
	bOrbitEditMode = false;

	// 关闭可视化（含清除覆盖格高亮）
	if (APlanetActor* Planet = Cast<APlanetActor>(UGameplayStatics::GetActorOfClass(GetWorld(), APlanetActor::StaticClass())))
	{
		if (UOrbitCoverageVisualComponent* Visual = Planet->GetOrbitCoverageVisualComponent())
		{
			Visual->SetVisualizationEnabled(false);
		}
	}

	// 恢复运行：先重置 StateTree 时间基准（暂停窗口不计入任务 dt——防"恢复即瞬移到目标"）
	if (USatelliteManagerComponent* Manager = ResolveSatelliteManager())
	{
		Manager->ResetSatelliteStateTreeTimeBase();
	}
	// 解除选定暂停（世界演化链恢复）
	if (UWorld* World = GetWorld())
	{
		if (USimulationControlSubsystem* SimControl = World->GetSubsystem<USimulationControlSubsystem>())
		{
			SimControl->SetSimulationPaused(false);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Player] Orbit edit mode EXITED"));
}

void ABasePlayerController::ToggleOrbitEditMode()
{
	if (bOrbitEditMode)
	{
		ExitOrbitEditMode();
	}
	else
	{
		EnterOrbitEditMode();
	}
}

bool ABasePlayerController::UpdateFirstOrbit(TFunctionRef<void(FRuntimeOrbitData&)> Mutator)
{
	APlanetActor* Planet = Cast<APlanetActor>(UGameplayStatics::GetActorOfClass(GetWorld(), APlanetActor::StaticClass()));
	if (!Planet)
	{
		UE_LOG(LogTemp, Warning, TEXT("[OrbitEdit] No PlanetActor found"));
		return false;
	}
	USatelliteManagerComponent* Manager = Planet->GetSatelliteManagerComponent();
	if (!Manager)
	{
		return false;
	}
	const TArray<FRuntimeOrbitData>& Orbits = Manager->GetRuntimeOrbits();
	if (Orbits.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[OrbitEdit] No runtime orbit available"));
		return false;
	}

	FRuntimeOrbitData Data = Orbits[0];
	Mutator(Data);
	Manager->UpdateOrbitData(Data.OrbitName, Data);   // 广播 → 可视化节流刷新
	return true;
}

void ABasePlayerController::OrbitSetServiceDistance(float ServiceDistance)
{
	const float NewValue = FMath::Max(0.f, ServiceDistance);
	if (UpdateFirstOrbit([NewValue](FRuntimeOrbitData& Data) { Data.ServiceDistance = NewValue; }))
	{
		UE_LOG(LogTemp, Log, TEXT("[OrbitEdit] ServiceDistance -> %.1f"), NewValue);
	}
}

void ABasePlayerController::OrbitSetRadius(float OrbitRadius)
{
	const float NewValue = FMath::Max(1.f, OrbitRadius);
	if (UpdateFirstOrbit([NewValue](FRuntimeOrbitData& Data) { Data.Radius = NewValue; }))
	{
		UE_LOG(LogTemp, Log, TEXT("[OrbitEdit] OrbitRadius -> %.1f"), NewValue);
	}
}

void ABasePlayerController::OrbitSetCenter(float X, float Y, float Z)
{
	const FVector NewCenter(X, Y, Z);
	if (UpdateFirstOrbit([NewCenter](FRuntimeOrbitData& Data) { Data.Transform.SetLocation(NewCenter); }))
	{
		UE_LOG(LogTemp, Log, TEXT("[OrbitEdit] Center -> %s"), *NewCenter.ToCompactString());
	}
}

void ABasePlayerController::OrbitSetNormal(float X, float Y, float Z)
{
	const FVector NewNormal = FVector(X, Y, Z).GetSafeNormal();
	if (NewNormal.IsNearlyZero())
	{
		UE_LOG(LogTemp, Warning, TEXT("[OrbitEdit] Invalid normal (zero vector)"));
		return;
	}
	if (UpdateFirstOrbit([NewNormal](FRuntimeOrbitData& Data)
		{
			// 与 CellManager 同源写法：MakeFromZ(...).Rotator()（TMatrix 无 ToQuaternion）
			const FRotator NewRotation = FRotationMatrix::MakeFromZ(NewNormal).Rotator();
			Data.Transform.SetRotation(NewRotation.Quaternion());
		}))
	{
		UE_LOG(LogTemp, Log, TEXT("[OrbitEdit] Normal -> %s"), *NewNormal.ToCompactString());
	}
}

USatelliteManagerComponent* ABasePlayerController::ResolveSatelliteManager() const
{
	APlanetActor* Planet = Cast<APlanetActor>(UGameplayStatics::GetActorOfClass(GetWorld(), APlanetActor::StaticClass()));
	return Planet ? Planet->GetSatelliteManagerComponent() : nullptr;
}

TArray<FName> ABasePlayerController::GetOrbitNames() const
{
	TArray<FName> Names;
	if (const USatelliteManagerComponent* Manager = ResolveSatelliteManager())
	{
		for (const FRuntimeOrbitData& Orbit : Manager->GetRuntimeOrbits())
		{
			Names.Add(Orbit.OrbitName);
		}
	}
	return Names;
}

bool ABasePlayerController::GetOrbitData(const FName OrbitName, FRuntimeOrbitData& OutData) const
{
	if (const USatelliteManagerComponent* Manager = ResolveSatelliteManager())
	{
		for (const FRuntimeOrbitData& Orbit : Manager->GetRuntimeOrbits())
		{
			if (Orbit.OrbitName == OrbitName)
			{
				OutData = Orbit;
				return true;
			}
		}
	}
	return false;
}

bool ABasePlayerController::SetOrbitData(const FRuntimeOrbitData& NewData) const
{
	if (USatelliteManagerComponent* Manager = ResolveSatelliteManager())
	{
		return Manager->UpdateOrbitData(NewData.OrbitName, NewData);
	}
	return false;
}

bool ABasePlayerController::CreateOrbit(const FRuntimeOrbitData& NewData) const
{
	if (USatelliteManagerComponent* Manager = ResolveSatelliteManager())
	{
		return Manager->AddOrbit(NewData);
	}
	return false;
}

void ABasePlayerController::DeleteOrbit(const FName OrbitName) const
{
	if (USatelliteManagerComponent* Manager = ResolveSatelliteManager())
	{
		Manager->RemoveOrbit(OrbitName);
	}
}

void ABasePlayerController::CommitOrbitChanges(const FName OrbitName) const
{
	if (USatelliteManagerComponent* Manager = ResolveSatelliteManager())
	{
		Manager->ApplyOrbitChangesForOrbit(OrbitName);   // 只重建该轨（选定暂停中执行，安全）
	}
}

void ABasePlayerController::OrbitShowEditor()
{
	// 已打开 → 关闭（Toggle）
	if (OrbitEditorPanel.IsValid())
	{
		CloseOrbitEditor();
		return;
	}

	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[OrbitEdit] GameViewport unavailable, cannot open editor panel"));
		return;
	}

	EnterOrbitEditMode();   // 进编辑模式（可视化开 + 悬停互斥）
	OrbitEditorPanel = SNew(SOrbitEditorPanel).PlayerController(this);
	GEngine->GameViewport->AddViewportWidgetContent(OrbitEditorPanel.ToSharedRef(), /*ZOrder=*/100);

	UE_LOG(LogTemp, Log, TEXT("[OrbitEdit] Editor panel OPENED"));
}

void ABasePlayerController::CloseOrbitEditor()
{
	if (OrbitEditorPanel.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(OrbitEditorPanel.ToSharedRef());
		}
		OrbitEditorPanel.Reset();
	}
	ExitOrbitEditMode();
	UE_LOG(LogTemp, Log, TEXT("[OrbitEdit] Editor panel CLOSED"));
}

// 切换到下一个编辑模式（正向循环）
void ABasePlayerController::SwitchToNextEditMode()
{
	constexpr int32 EditModeCount = static_cast<int32>(EEditMode::Count);
	int32 Index = static_cast<int32>(EditMode);
	Index = (Index + 1) % EditModeCount;
	EditMode = static_cast<EEditMode>(Index);
}

// 切换到上一个编辑模式（反向循环）
void ABasePlayerController::SwitchToPrevEditMode()
{
	constexpr int32 EditModeCount = static_cast<int32>(EEditMode::Count);
	int32 Index = static_cast<int32>(EditMode);
	Index = (Index - 1 + EditModeCount) % EditModeCount; // 避免负数
	EditMode = static_cast<EEditMode>(Index);
}