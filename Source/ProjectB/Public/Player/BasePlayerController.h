// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Crops/CropData.h"
#include "GameFramework/PlayerController.h"
#include "Game/ProjectBGameInstance.h"
#include "BasePlayerController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEditModeChanged, EEditMode, NewEditMode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHoveredPlot, AFarmlandActor*, HoveredFarmlandActor, FIntPoint, HoveredPlotCoords);


class AFarmlandActor;
class UTimeSyncComponent;
/**
 * 
 */
UCLASS()
class PROJECTB_API ABasePlayerController : public APlayerController
{
	GENERATED_BODY()

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
private:
	// 编辑模式
	EEditMode EditMode = EEditMode::None;
	// 射线检测获取点击的农场和单元格
	bool TraceForFarmlandAndCell(FVector& OutHitLocation, AFarmlandActor*& OutFarmland, FIntPoint& OutCellCoords);

	void UpdateHoveredPlot();

	UPROPERTY()
	TObjectPtr<AFarmlandActor> PreHoveredFarmland;
	
	UPROPERTY()
	FIntPoint PreHoveredPlotCoords = FIntPoint(-1, -1);

	UPROPERTY()
	FString SelectedCropName;
	
	UPROPERTY()
	TObjectPtr<UProjectBGameInstance> GameInstance;
};
