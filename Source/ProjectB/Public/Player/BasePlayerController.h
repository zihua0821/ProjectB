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

	/** 控制台测试命令：ToggleOrbitEditMode */
	UFUNCTION(Exec)
	void ToggleOrbitEditMode();

	// ---- 轨道参数控制台调试命令（测试/演示用；正式入口为 UMG 面板） ----

	/** 设置第一条轨道的服务距离（覆盖半高）：OrbitSetServiceDistance 5000 */
	UFUNCTION(Exec)
	void OrbitSetServiceDistance(float ServiceDistance);

	/** 设置第一条轨道的轨道半径：OrbitSetRadius 55000 */
	UFUNCTION(Exec)
	void OrbitSetRadius(float OrbitRadius);

	/** 设置第一条轨道的圆心偏移：OrbitSetCenter X Y Z */
	UFUNCTION(Exec)
	void OrbitSetCenter(float X, float Y, float Z);

	/** 设置第一条轨道的平面法线（自动归一化）：OrbitSetNormal X Y Z */
	UFUNCTION(Exec)
	void OrbitSetNormal(float X, float Y, float Z);

	/** 应用轨道变更（重建卫星实体）：OrbitApplyChanges */
	UFUNCTION(Exec)
	void OrbitApplyChanges();

	// ---- UMG 轨道编辑面板接口（BlueprintCallable；内部转发到 SatelliteManager） ----

	/** 全部轨道名（面板列表用） */
	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	TArray<FName> GetOrbitNames() const;

	/** 读取指定轨道数据（面板初始化/切换选中） */
	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	bool GetOrbitData(FName OrbitName, FRuntimeOrbitData& OutData) const;

	/** 写入轨道数据（面板拖动实时预览；不重建卫星） */
	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	bool SetOrbitData(const FRuntimeOrbitData& NewData) const;

	/** 新建轨道（成功后重建全部卫星） */
	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	bool CreateOrbit(const FRuntimeOrbitData& NewData) const;

	/** 删除轨道（成功后重建剩余轨道的卫星） */
	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	void DeleteOrbit(FName OrbitName) const;

	/** 应用轨道变更：重建全部卫星实体 */
	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	void ApplyOrbitChangesToSatellites() const;

	/** 显示/隐藏轨道编辑面板（控制台命令）：OrbitShowEditor */
	UFUNCTION(Exec)
	void OrbitShowEditor();

	/** 关闭轨道编辑面板（面板内部调用） */
	void CloseOrbitEditor();

	UFUNCTION(BlueprintCallable, Category = "OrbitEdit")
	bool IsOrbitEditMode() const { return bOrbitEditMode; }

	/** 轨道编辑模式开关（进入时悬停高亮禁用，覆盖格高亮独占轮廓层） */
	bool bOrbitEditMode = false;

	/** 修改第一条轨道的指定参数（控制台调试命令共用；返回成功与否） */
	bool UpdateFirstOrbit(TFunctionRef<void(FRuntimeOrbitData&)> Mutator);

	/** 解析场景中的卫星管理组件（UMG 接口共用；失败返回 nullptr） */
	class USatelliteManagerComponent* ResolveSatelliteManager() const;

	/** 轨道编辑面板（Slate；打开时创建） */
	TSharedPtr<class SOrbitEditorPanel> OrbitEditorPanel;

	// 编辑模式
	EEditMode EditMode = EEditMode::None;
	// 射线检测获取点击的农场和单元格
	bool TraceForFarmlandAndCell(FVector& OutHitLocation, APlanetActor*& OutPlanetActor, FIntPoint& OutCellCoords);

	// 执行编辑操作（玩家认领守卫：已启用 Slot 走认领协议，未启用直接执行）
	void ExecuteEditModeWithClaim(APlanetActor* Planet, const FIntPoint CellCoords);

	void UpdateHoveredPlot();

	UPROPERTY()
	TObjectPtr<APlanetActor> PreHoveredPlanetActor;
	
	UPROPERTY()
	FIntPoint PreHoveredPlotCoords = FIntPoint(-1, -1);

	UPROPERTY()
	FString SelectedCropName;
	
	UPROPERTY()
	TObjectPtr<UProjectBGameInstance> GameInstance;
};
