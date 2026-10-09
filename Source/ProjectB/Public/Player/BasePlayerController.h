// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Crops/CropData.h"
#include "GameFramework/PlayerController.h"
#include "Game/ProjectBGameInstance.h"
#include "Mass/SatelliteManagerComponent.h"   // FRuntimeOrbitData（轨道调试命令参数）
#include "BasePlayerController.generated.h"

class APlanetActor;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEditModeChanged, EEditMode, NewEditMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHoveredPlot, APlanetActor*, HoveredPlanetActor, FIntPoint, HoveredPlotCoords);


class UTimeSyncComponent;
/**
 * 
 */
UCLASS()
class PROJECTB_API ABasePlayerController : public APlayerController
{
	GENERATED_BODY()

	/** 轨道编辑面板（Slate）可访问本控制器的轨道接口（protected 区） */
	friend class SOrbitEditorPanel;

	/** 轨道编辑面板的 UMG 包装控件（同样需访问轨道接口） */
	friend class UOrbitEditorWidget;

public:
	ABasePlayerController();

	UFUNCTION(BlueprintCallable)
	void SetEditMode(const EEditMode InEditMode);

	UFUNCTION(BlueprintCallable)
	EEditMode GetEditMode() const {return EditMode;}

	UFUNCTION(BlueprintCallable)
	void SwitchToNextEditMode();

	UFUNCTION(BlueprintCallable)
	void SwitchToPrevEditMode();

	UFUNCTION(BlueprintCallable)
	void SetSelectedCropName(const FString& InCropName) {SelectedCropName = InCropName;}

	UFUNCTION(BlueprintCallable)
	FString GetSelectedCropName() const {return SelectedCropName;}
	
	UPROPERTY(BlueprintAssignable)
	FOnEditModeChanged OnEditModeChanged;

	UPROPERTY(BlueprintAssignable)
	FOnHoveredPlot OnHoveredPlot;
protected:
	virtual void BeginPlay() override;

	virtual void Tick(float DeltaSeconds) override;
	// 时间同步组件
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Time Sync")
	TObjectPtr<UTimeSyncComponent> TimeSyncComponent;

	UFUNCTION(BlueprintCallable)
	void OnLeftMouseClick();

	// ---- 轨道编辑模式（进入时开启轨道覆盖可视化；悬停高亮与之互斥） ----

	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	void EnterOrbitEditMode();

	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	void ExitOrbitEditMode();

	// ---- 轨道编辑面板接口（C++；内部转发到 SatelliteManager——经 friend 供面板访问） ----

	/** 全部轨道名（面板列表用） */
	TArray<FName> GetOrbitNames() const;

	/** 读取指定轨道数据（面板初始化/切换选中） */
	bool GetOrbitData(FName OrbitName, FRuntimeOrbitData& OutData) const;

	/** 写入轨道数据（面板拖动实时预览；不重建卫星） */
	bool SetOrbitData(const FRuntimeOrbitData& NewData) const;

	/** 新建轨道（成功后重建全部卫星） */
	bool CreateOrbit(const FRuntimeOrbitData& NewData) const;

	/** 删除轨道（成功后重建剩余轨道的卫星） */
	void DeleteOrbit(FName OrbitName) const;

	/** 提交编辑变更（参数控件的回车/失焦落定）：只重建指定轨道——所见即所得 */
	void CommitOrbitChanges(FName OrbitName) const;

	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	bool IsOrbitEditMode() const { return bOrbitEditMode; }

	/** 轨道编辑模式开关（进入时悬停高亮禁用，覆盖格高亮独占轮廓层） */
	bool bOrbitEditMode = false;

	/** 解析场景中的卫星管理组件（面板接口共用；失败返回 nullptr） */
	class USatelliteManagerComponent* ResolveSatelliteManager() const;

	// 编辑模式
	EEditMode EditMode = EEditMode::None;
	// 射线检测获取点击的农场和单元格
	bool TraceForFarmlandAndCell(FVector& OutHitLocation, APlanetActor*& OutPlanetActor, FIntPoint& OutCellCoords);

	// 执行编辑操作（玩家认领守卫：已启用 Slot 走认领协议，未启用直接执行）
	void ExecuteEditModeWithClaim(APlanetActor* Planet, const FIntPoint CellCoords);

	void UpdateHoveredPlot();

	UPROPERTY()
	TWeakObjectPtr<APlanetActor> PreHoveredPlanetActor;
	
	UPROPERTY()
	FIntPoint PreHoveredPlotCoords = FIntPoint(-1, -1);

	UPROPERTY()
	FString SelectedCropName;
	
	UPROPERTY()
	TObjectPtr<UProjectBGameInstance> GameInstance;
};
