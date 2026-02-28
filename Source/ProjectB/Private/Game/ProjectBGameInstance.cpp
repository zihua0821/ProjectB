// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/ProjectBGameInstance.h"

#include "Farm/FarmlandActor.h"
#include "Kismet/GameplayStatics.h"
#include "Tools/FarmSaveGame.h"

void UProjectBGameInstance::SaveGame(FString SlotName)
{
	// 如果还没有存档对象，创建一个新的
	if (!CurrentSaveGame)
	{
		CurrentSaveGame = Cast<UFarmSaveGame>(
			UGameplayStatics::CreateSaveGameObject(UFarmSaveGame::StaticClass())
		);
	}

	// 1. 收集全局数据（从你的全局管理器获取）
	CurrentSaveGame->GlobalData.GameTime=  FDateTime::UtcNow();

	// 2. 收集所有地块数据
	CurrentSaveGame->Farmlands.Empty();

	// 找到场景中的 FarmlandActor
	TArray<AActor*> FindActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFarmlandActor::StaticClass(), FindActors);
	for (AActor* FindActor : FindActors)
	{
		if (AFarmlandActor* Farmland = Cast<AFarmlandActor>(FindActor))
		{
			FFarmlandSaveGameData FarmlandData;
			FarmlandData.Transform = Farmland->GetActorTransform();
			FarmlandData.CropDataTablePath = Farmland->CropDataTable;
			FarmlandData.FarmlandConfigPath = Farmland->FarmlandConfig;
			FarmlandData.Plots = Farmland->GetPlotSaveGameData();
			FarmlandData.Crops = Farmland->GetCropSaveGameData();
			CurrentSaveGame->Farmlands.Add(FarmlandData);
		}
	}

	// 3. 异步保存
	UGameplayStatics::AsyncSaveGameToSlot(CurrentSaveGame, SlotName, 0,
		FAsyncSaveGameToSlotDelegate::CreateLambda([](const FString& Slot, int32 UserIndex, bool bSuccess)
		{
			UE_LOG(LogTemp, Log, TEXT("Save game %s: %s"), *Slot, bSuccess ? TEXT("Success") : TEXT("Failed"));
		})
	);
}

void UProjectBGameInstance::LoadGame(FString SlotName)
{
	UGameplayStatics::AsyncLoadGameFromSlot(SlotName, 0, FAsyncLoadGameFromSlotDelegate::CreateUObject(this, &UProjectBGameInstance::OnGameLoaded));
}

void UProjectBGameInstance::OnGameLoaded(const FString& SlotName, const int32 UserIndex, USaveGame* SaveGame)
{
	if (SaveGame)
	{
		CurrentSaveGame = Cast<UFarmSaveGame>(SaveGame);
		if (CurrentSaveGame)
		{
			// 清除场景中已有的FarmlandActor
			TArray<AActor*> ExistingFarmlandActors;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), AFarmlandActor::StaticClass(), ExistingFarmlandActors);
			for (AActor* Actor : ExistingFarmlandActors)
			{
				if (AFarmlandActor* FarmlandActor = Cast<AFarmlandActor>(Actor))
				{
					FarmlandActor->Destroy();
				}
			}

			// 重建场景中的FarmlandActor
			for (const FFarmlandSaveGameData& FarmlandData : CurrentSaveGame->Farmlands)
			{
				UFarmlandConfigDataAsset* LoadedConfig = FarmlandData.FarmlandConfigPath.LoadSynchronous();
				UDataTable* LoadedDataTable = FarmlandData.CropDataTablePath.LoadSynchronous();
				if (!LoadedConfig || !LoadedDataTable)
				{
					UE_LOG(LogTemp, Error, TEXT("Load game %s: Failed to load config or datatable"), *SlotName)
				}
				AFarmlandActor* Farmland = GetWorld()->SpawnActorDeferred<AFarmlandActor>(AFarmlandActor::StaticClass(), FarmlandData.Transform);
				Farmland->FarmlandConfig = LoadedConfig;
				Farmland->CropDataTable = LoadedDataTable;
				Farmland->SetPlotSaveGameData(FarmlandData.Plots);
				Farmland->SetCropSaveGameData(FarmlandData.Crops);
				Farmland->FinishSpawning(FarmlandData.Transform);
			}
		}
		
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Load game %s: Failed"), *SlotName);
	}
}
