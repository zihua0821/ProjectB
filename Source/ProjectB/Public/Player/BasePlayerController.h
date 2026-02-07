// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Crops/CropData.h"
#include "GameFramework/PlayerController.h"
#include "BasePlayerController.generated.h"

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
	void SetEditMode(const EEditMode InEditMode){EditMode = InEditMode;}

	UFUNCTION(BlueprintCallable)
	EEditMode GetEditMode() const {return EditMode;}

	UFUNCTION(BlueprintCallable)
	void SwitchToNextEditMode();

	UFUNCTION(BlueprintCallable)
	void SwitchToPrevEditMode();
protected:
	virtual void BeginPlay() override;
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

};
