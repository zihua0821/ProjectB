// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/ProjectBGameInstance.h"

#include "Farm/FarmlandActor.h"
#include "Farm/FarmlandConfigDataAsset.h"      // TSoftObjectPtr 赋值 / LoadSynchronous 需完整类型
#include "Kismet/GameplayStatics.h"
#include "Planet/PlanetActor.h"
#include "Planet/PlanetConfigDataAsset.h"       // 同上（完整类型）
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

	// 2. 收集所有数据
	CurrentSaveGame->Planets.Empty();

	// 找到场景中的 PlanetActor
	TArray<AActor*> FindActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlanetActor::StaticClass(), FindActors);
	for (AActor* FindActor : FindActors)
	{
		if (APlanetActor* Planet = Cast<APlanetActor>(FindActor))
		{
			FPlanetSaveGameData PlanetSaveGameData;
			PlanetSaveGameData.Transform = Planet->GetActorTransform();
			PlanetSaveGameData.PlanetConfigPath = Planet->PlanetConfig;
			PlanetSaveGameData.FarmlandConfigPath = Planet->FarmlandConfig;
			PlanetSaveGameData.CropDataTablePath = Planet->CropDataTable;
			PlanetSaveGameData.Plots = Planet->GetPlotSaveGameData();
			PlanetSaveGameData.Crops = Planet->GetCropSaveGameData();
			PlanetSaveGameData.PlanetInfo = IPlanetInterface::Execute_GetPlanetInfo(Planet);
			
			CurrentSaveGame->Planets.Add(PlanetSaveGameData);
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
			// 清除场景中已有的 PlanetActor
			TArray<AActor*> ExistingFarmlandActors;
			UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlanetActor::StaticClass(), ExistingFarmlandActors);
			for (AActor* Actor : ExistingFarmlandActors)
			{
				if (APlanetActor* PlanetActor = Cast<APlanetActor>(Actor))
				{
					PlanetActor->Destroy();
				}
			}

			// 重建场景中的 PlanetActor
			for (const FPlanetSaveGameData& PlanetSaveGameData : CurrentSaveGame->Planets)
			{
				UPlanetConfigDataAsset* LoadedPlanetConfig = PlanetSaveGameData.PlanetConfigPath.LoadSynchronous();
				UFarmlandConfigDataAsset* LoadedConfig = PlanetSaveGameData.FarmlandConfigPath.LoadSynchronous();
				UDataTable* LoadedDataTable = PlanetSaveGameData.CropDataTablePath.LoadSynchronous();
				
				if (!LoadedConfig || !LoadedPlanetConfig || !LoadedDataTable)
				{
					UE_LOG(LogTemp, Error, TEXT("Load game %s: Failed to load config or datatable"), *SlotName)
				}
				
				APlanetActor* Planet = GetWorld()->SpawnActorDeferred<APlanetActor>(APlanetActor::StaticClass(), PlanetSaveGameData.Transform);
				Planet->FarmlandConfig = LoadedConfig;
				Planet->CropDataTable = LoadedDataTable;
				Planet->PlanetConfig = LoadedPlanetConfig;
				Planet->SetPlotSaveGameData(PlanetSaveGameData.Plots);
				Planet->SetCropSaveGameData(PlanetSaveGameData.Crops);
				IPlanetInterface::Execute_SetPlanetInfo(Planet, PlanetSaveGameData.PlanetInfo);
				Planet->FinishSpawning(PlanetSaveGameData.Transform);
			}
		}
		
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Load game %s: Failed"), *SlotName);
	}
}
