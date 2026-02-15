// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "ProjectBGameInstance.generated.h"

class USaveGame;
class UFarmSaveGame;
/**
 * 
 */
UCLASS()
class PROJECTB_API UProjectBGameInstance : public UGameInstance
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable)
	void SaveGame(FString SlotName = "SaveSlot1");

	UFUNCTION(BlueprintCallable)
	void LoadGame(FString SlotName = "SaveSlot1");

	// 获取存档数据（供其他系统查询）
	UFUNCTION(BlueprintPure)
	UFarmSaveGame* GetCurrentSaveGame() const { return CurrentSaveGame; }

private:
	UPROPERTY()
	TObjectPtr<UFarmSaveGame> CurrentSaveGame = nullptr;

	UFUNCTION()
	void OnGameLoaded(const FString& SlotName, const int32 UserIndex, USaveGame* SaveGame);
};
