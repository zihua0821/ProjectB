#include "Farm/CropManagerComponent.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Farm/FarmlandComponent.h"
#include "Crops/CropBase.h"
#include "Planet/PlanetData.h"
#include "Planet/PlanetInterface.h"
#include "Player/BasePlayerController.h"
#include "Subsystem/ISMManager.h"
#include "Tools/ISMTools.h"

UCropManagerComponent::UCropManagerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 1.f;
}

void UCropManagerComponent::Initialize(UFarmlandComponent* InFarmlandComponent)
{
    FarmlandComponent = InFarmlandComponent;
    if (FarmlandComponent.IsValid())
    {
        // 绑定地块事件
        FarmlandComponent->OnPlotStateChanged.AddDynamic(this, &UCropManagerComponent::HandlePlotStateChanged);
        FarmlandComponent->OnPlotMoistureChanged.AddDynamic(this, &UCropManagerComponent::HandlePlotMoistureChanged);
        FarmlandComponent->OnPlotFertilityChanged.AddDynamic(this, &UCropManagerComponent::HandlePlotFertilityChanged);
    }
}

void UCropManagerComponent::HandleLeftMouseClicked(const EEditMode EditMode, const FIntPoint Coords)
{
    if (!BasePlayerController)
    {
        return;
    }
    switch (EditMode) {
    case EEditMode::PlantCrop:
        PlantCrop(Coords, BasePlayerController->GetSelectedCropName());
        break;
    case EEditMode::HarvestCrop:
        HarvestCrop(Coords);
        break;
    case EEditMode::CropDried:
        CropDried(Coords);
        break;
    case EEditMode::UnCropDried:
        RemoveCropDried(Coords);
        break;
    case EEditMode::RemoveCrop:
        RemoveCrop(Coords);
        break;
    default:
        break;
    }
}

void UCropManagerComponent::BeginPlay()
{
    Super::BeginPlay();

    OwnerActor = GetOwner();
    
    BasePlayerController = Cast<ABasePlayerController>(GetWorld()->GetFirstPlayerController());

    InitializeCrops();
}

void UCropManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UpdateCrops(DeltaTime);
}

void UCropManagerComponent::CropDried(FIntPoint Coords)
{
    SetCropDisease(Coords, true, true);
}

void UCropManagerComponent::RemoveCropDried(FIntPoint Coords)
{
    SetCropDisease(Coords, false, false);
}

void UCropManagerComponent::RemoveCrop(FIntPoint Coords)
{
    if (!PlotCropMap.Contains(Coords)) return;
    const FPlotCropInfo& CropInfo = PlotCropMap[Coords];
    if (!CropInfo.Crop) return;

    // 清理渲染实例
    UStaticMesh* CurrentMesh = nullptr;
    switch (CropInfo.Crop->GetCropData().CropGrowthStage)
    {
    case ECropGrowthStage::Seed:      CurrentMesh = CropInfo.CropMeshVariant.SeedMesh; break;
    case ECropGrowthStage::Seedling:  CurrentMesh = CropInfo.CropMeshVariant.SeedlingMesh; break;
    case ECropGrowthStage::Mature:    CurrentMesh = CropInfo.CropMeshVariant.MatureMesh; break;
    case ECropGrowthStage::Harvested: CurrentMesh = CropInfo.CropMeshVariant.HarvestedMesh; break;
    default: break;
    }
    if (CurrentMesh)
    {
        // TODO: 迁移到 ISMManager 原生增量接口（BatchRemoveInstances），需先保存 FGlobalInstanceId
        
        // if (CropHISMComponentMap.Contains(FStaticMeshKey(CurrentMesh)))
        // {
        //     if (UInstancedStaticMeshComponent* CurrentInstanceHISMComponent = CropHISMComponentMap.FindChecked(FStaticMeshKey(CurrentMesh)))
        //     {
        //         UISMTools::RemoveInstanceByTransform(CurrentInstanceHISMComponent, CropInfo.CropTransform);
        //     }
        //     else
        //     {
        //         UE_LOG(LogTemp, Log, TEXT("CurrentInstanceHISMComponent not found for %s"), *CurrentMesh->GetName());
        //         return;
        //     }
        // }
    }

    // 解绑事件
    // CropInfo.Crop->OnPlantGrowthStageChanged.RemoveAll(this);
    // CropInfo.Crop->OnCropGrownThisTime.Unbind();

    // 移除实例
    PlotCropMap.Remove(Coords);
    const FVector CropLocation = GetComponentTransform().TransformPosition(CropInfo.CropTransform.GetLocation());
    
    // 更新地块状态
    FarmlandComponent->SetPlotState(Coords, EPlotState::CropPlanted, true);
    
    OnCropPlantedOrRemoved.Broadcast(Coords);

    PlantCrop(Coords, BasePlayerController->GetSelectedCropName());
}

void UCropManagerComponent::PlantCrop(FIntPoint Coords, const FString& CropName)
{
    if (!OwnerActor.IsValid()) return;
    if (!FarmlandComponent.IsValid()) return;

    // 检查地块是否可种植（已耕地、无作物）
    FFarmLandPlotInfo PlotInfo = FarmlandComponent->GetPlotInfo(Coords);
    if (PlotInfo.StateData.IsEmpty() || PlotCropMap.Contains(Coords)) return;

    // 从数据表读取作物配置
    FName CropID = FName(CropName);
    const FCropTableRow* Row = CropDataTable->FindRow<FCropTableRow>(CropID, TEXT("PlantCrop"));
    if (!Row) return;

    // 处理星球能量消耗
    IPlanetInterface* PlanetInterface = Cast<IPlanetInterface>(OwnerActor.Get());
    auto& [PlanetPower] = PlanetInterface->GetPlanetInfoRef();
    if (PlanetPower < Row->SeedPrice) return;
    PlanetPower -= Row->SeedPrice;
    IPlanetInterface::Execute_CallPlanetInfoChanged(OwnerActor.Get());

    // 创建作物实例
    UCropBase* NewCrop = NewObject<UCropBase>(this);
    int32 VariantIndex = FMath::RandRange(0, Row->MeshVariants.Num() - 1);

    // 准备作物数据
    FCropData CropData;
    CropData.CropID = CropID;
    CropData.Coords = Coords;
    CropData.VariantsIndex = VariantIndex;
    CropData.GrowthSeedTime = Row->GrowthSeedTime;
    CropData.GrowthSeedlingTime = Row->GrowthSeedlingTime;
    CropData.MoistureConsumePerSecond = Row->MoistureConsumePerSecond;
    CropData.FertilityConsumePerSecond = Row->FertilityConsumePerSecond;
    CropData.GrowthRate_OverWatered = FarmlandConfig->GrowthRate_OverWatered;
    CropData.GrowthRate_NutrientBurn = FarmlandConfig->GrowthRate_NutrientBurn;
    CropData.GrowthRate_WeedInfested = FarmlandConfig->GrowthRate_WeedInfested;
    CropData.GrowthRate_PestInfested = FarmlandConfig->GrowthRate_PestInfested;
    //----------
    CropData.TotalGrowthTime = FMath::RandRange(0.f, Row->GrowthSeedTime + Row->GrowthSeedlingTime);
    //----------
    
    // 根据地块初始状态调整作物状态
    if (PlotInfo.StateData.HasState(EPlotState::Watered))
        CropData.CropStateData.RemoveState(ECropState::NeedsWater);
    if (PlotInfo.StateData.HasState(EPlotState::Fertilized))
        CropData.CropStateData.RemoveState(ECropState::NeedsFertilizer);
    if (PlotInfo.StateData.HasState(EPlotState::PestInfested))
        CropData.CropStateData.AddState(ECropState::PestInfested);
    if (PlotInfo.StateData.HasState(EPlotState::WeedInfested))
        CropData.CropStateData.AddState(ECropState::WeedInfested);

    // 存储作物信息
    FPlotCropInfo CropInfo;
    CropInfo.Crop = NewCrop;
    CropInfo.CropMeshVariant = Row->MeshVariants[VariantIndex];
    CropInfo.CropNumCustomDataFloats = Row->CropNumCustomDataFloats;

    // 计算作物位置
    FTransform PlotCropTransform = PlotInfo.Transform;
    PlotCropTransform.AddToTranslation(FarmlandConfig->CropOffset);
    CropInfo.CropTransform = PlotCropTransform;
    
    // 计算作物世界位置
    FVector CropWorldLocation = GetComponentTransform().TransformPosition(PlotCropTransform.GetLocation());
    PlotCropMap.Add(Coords, CropInfo);
   
    // 绑定作物自身的事件
    // NewCrop->OnPlantGrowthStageChanged.AddDynamic(this, &ThisClass::OnCropGrowthStageChanged);
    // NewCrop->OnCropGrownThisTime.BindDynamic(this, &ThisClass::OnCropGrownThisTime);
    
    // 初始化作物（传入本组件引用，以便作物能绑定地块事件）
    NewCrop->InitializeCrop(this, CropData);

    // 更新地块状态
    FarmlandComponent->SetPlotState(Coords, EPlotState::CropPlanted, true);
    
    // 广播事件
    OnCropPlantedOrRemoved.Broadcast(Coords);
}

bool UCropManagerComponent::HarvestCrop(FIntPoint Coords)
{
    if (!OwnerActor.IsValid()) return  false;
    if (!PlotCropMap.Contains(Coords)) return false;
    FPlotCropInfo& CropInfo = PlotCropMap[Coords];
    if (!CropInfo.Crop || CropInfo.Crop->GetCropData().CropGrowthStage != ECropGrowthStage::Mature)
        return false;
    
    const FCropTableRow* Row = CropDataTable->FindRow<FCropTableRow>(CropInfo.Crop->GetCropData().CropID, TEXT("HarvestCrop"));
    if (!Row) return false;
    CropInfo.Crop->HarvestCrop();

    FCropData CropData = CropInfo.Crop->GetCropData();

    float SalePriceMultiplier = 1.0f;
    switch (CropData.CropQualityData.GetQuality(CropData.TotalGrowthTime)) {
    case ECropQuality::Poor:
        SalePriceMultiplier = FarmlandConfig->SalePriceMultiplier_Poor;
        break;
    case ECropQuality::Normal:
        SalePriceMultiplier = FarmlandConfig->SalePriceMultiplier_Normal;
        break;
    case ECropQuality::Good:
        SalePriceMultiplier = FarmlandConfig->SalePriceMultiplier_Good;
        break;
    case ECropQuality::Excellent:
        SalePriceMultiplier = FarmlandConfig->SalePriceMultiplier_Excellent;
        break;
    }
    const int32 CropSalePrice = FMath::RoundToInt32(Row->BaseSalePrice * SalePriceMultiplier);
    // 处理星球能量增加
    IPlanetInterface* PlanetInterface = Cast<IPlanetInterface>(OwnerActor.Get());
    auto& [PlanetPower] = PlanetInterface->GetPlanetInfoRef();
    PlanetPower += CropSalePrice;
    IPlanetInterface::Execute_CallPlanetInfoChanged(OwnerActor.Get());
    
    return true;
}

void UCropManagerComponent::InitCropsFromSaveGameData()
{
    if (CropSaveGameData.Num() > 0)
    {
        for (auto& CurrentCropSaveGameData: CropSaveGameData)
        {
            if (!FarmlandComponent.IsValid()) return;

            FIntPoint Coords = CurrentCropSaveGameData.Coords;

            // 从数据表读取作物配置
            FName CropID = CurrentCropSaveGameData.CropID;
            const FCropTableRow* Row = CropDataTable->FindRow<FCropTableRow>(CropID, TEXT("InitCropsFromSaveGameData"));
            if (!Row) return;

            // 创建作物实例
            UCropBase* SaveCrop = NewObject<UCropBase>(this);

            // 恢复作物数据
            FCropData CropData;
            CropData.CropID = CropID;
            CropData.Coords = Coords;
            CropData.CropGrowthStage = CurrentCropSaveGameData.CropGrowthStage;
            CropData.CropQualityData = CurrentCropSaveGameData.CropQualityData;
            CropData.CropStateData = CurrentCropSaveGameData.CropStateData;
            CropData.TotalGrowthTime = CurrentCropSaveGameData.TotalGrowthTime;
            CropData.CurrentStageGrowthTime = CurrentCropSaveGameData.CurrentStageGrowthTime;
            CropData.VariantsIndex = CurrentCropSaveGameData.VariantsIndex;

            // 根据数据表配置
            CropData.GrowthSeedTime = Row->GrowthSeedTime;
            CropData.GrowthSeedlingTime = Row->GrowthSeedlingTime;
            CropData.MoistureConsumePerSecond = Row->MoistureConsumePerSecond;
            CropData.FertilityConsumePerSecond = Row->FertilityConsumePerSecond;
            CropData.GrowthRate_OverWatered = FarmlandConfig->GrowthRate_OverWatered;
            CropData.GrowthRate_NutrientBurn = FarmlandConfig->GrowthRate_NutrientBurn;
            CropData.GrowthRate_WeedInfested = FarmlandConfig->GrowthRate_WeedInfested;
            CropData.GrowthRate_PestInfested = FarmlandConfig->GrowthRate_PestInfested;
            
            // 存储作物信息
            FPlotCropInfo CropInfo;
            CropInfo.Crop = SaveCrop;
            CropInfo.CropMeshVariant = Row->MeshVariants[CurrentCropSaveGameData.VariantsIndex];
            CropInfo.CropNumCustomDataFloats = Row->CropNumCustomDataFloats;
            CropInfo.CropTransform = CurrentCropSaveGameData.Transform;
            PlotCropMap.Add(Coords, CropInfo);
            FVector CropWorldLocation = GetComponentTransform().TransformPosition(CropInfo.CropTransform.GetLocation());
            
            // 绑定作物自身的事件
            // SaveCrop->OnPlantGrowthStageChanged.AddDynamic(this, &ThisClass::OnCropGrowthStageChanged);
            // SaveCrop->OnCropGrownThisTime.BindDynamic(this, &ThisClass::OnCropGrownThisTime);
            
            // 初始化作物（传入本组件引用，以便作物能绑定地块事件）
            SaveCrop->InitializeCrop(this, CropData);

            // 更新地块状态
            FarmlandComponent->SetPlotState(Coords, EPlotState::CropPlanted, true);
        }
    }
}

void UCropManagerComponent::InitializeCrops()
{
    InitCropsFromSaveGameData();
}

void UCropManagerComponent::UpdateCrops(float DeltaTime)
{
    for (auto& Pair : PlotCropMap)
    {
        if (Pair.Value.Crop)
        {
            if (Pair.Value.Crop->UpdateCrop(DeltaTime))
            {
                // const FCropData& CropData = Pair.Value.Crop->GetCropData();
                // OnCropGrownThisTime(
                //     Pair.Key,
                //     CropData.MoistureConsumePerSecond * DeltaTime,
                //     CropData.FertilityConsumePerSecond * DeltaTime
                //     );
                OnCropGrownThisTime(
                    Pair.Key,
                    0.f,
                    0.f
                    );
            }
        }
    }
}

UCropBase* UCropManagerComponent::GetCropAt(FIntPoint Coords) const
{
    if (const FPlotCropInfo* Info = PlotCropMap.Find(Coords))
        return Info->Crop;
    return nullptr;
}

FPlotCropInfo UCropManagerComponent::GetPlotCropInfo(FIntPoint Coords) const
{
    if (PlotCropMap.Contains(Coords))
    {
        return *PlotCropMap.Find(Coords);
    }
    return FPlotCropInfo();
}

TArray<FCropSaveGameData> UCropManagerComponent::GetCropSaveGameData()
{
    CropSaveGameData.Empty();
    for (auto& Pair : PlotCropMap)
    {
        const FPlotCropInfo& CropInfo = Pair.Value;
        if (!CropInfo.Crop) continue;
        const FCropData& CropData = CropInfo.Crop->GetCropData();

        // 保存作物数据
        FCropSaveGameData CurrentCropSaveGameData;
        CurrentCropSaveGameData.CropID = CropData.CropID;
        CurrentCropSaveGameData.Coords = Pair.Key;
        CurrentCropSaveGameData.Transform = CropInfo.CropTransform;
        CurrentCropSaveGameData.VariantsIndex = CropData.VariantsIndex;
        CurrentCropSaveGameData.CropGrowthStage = CropData.CropGrowthStage;
        CurrentCropSaveGameData.CropQualityData = CropData.CropQualityData;
        CurrentCropSaveGameData.CropStateData = CropData.CropStateData;
        CurrentCropSaveGameData.TotalGrowthTime = CropData.TotalGrowthTime;
        CurrentCropSaveGameData.CurrentStageGrowthTime = CropData.CurrentStageGrowthTime;
        
        CropSaveGameData.Add(CurrentCropSaveGameData);
    }
    return CropSaveGameData;
}

void UCropManagerComponent::OnCropGrowthStageChanged(FIntPoint Coords, ECropGrowthStage NewGrowthStage)
{
    if (!PlotCropMap.Contains(Coords)) return;
    FPlotCropInfo& CropInfo = PlotCropMap[Coords];

    // 确定旧网格和新网格
    UStaticMesh* OldMesh = nullptr;
    UStaticMesh* NewMesh = nullptr;

    switch (NewGrowthStage)
    {
    case ECropGrowthStage::Seed:
        NewMesh = CropInfo.CropMeshVariant.SeedMesh;
        break;
    case ECropGrowthStage::Seedling:
        OldMesh = CropInfo.CropMeshVariant.SeedMesh;
        NewMesh = CropInfo.CropMeshVariant.SeedlingMesh;
        break;
    case ECropGrowthStage::Mature:
        OldMesh = CropInfo.CropMeshVariant.SeedlingMesh;
        NewMesh = CropInfo.CropMeshVariant.MatureMesh;
        break;
    case ECropGrowthStage::Harvested:
        OldMesh = CropInfo.CropMeshVariant.MatureMesh;
        NewMesh = CropInfo.CropMeshVariant.HarvestedMesh;
        break;
    default:
        break;
    }
    
    // TODO: 迁移到 ISMManager 原生增量接口（旧实例 BatchRemoveInstances）
    
    // 移除旧实例
    if (OldMesh)
    {
        // if (CropHISMComponentMap.Contains(FStaticMeshKey(OldMesh)))
        // {
        //     if (UInstancedStaticMeshComponent* OldInstanceHISMComponent = CropHISMComponentMap.FindChecked(FStaticMeshKey(OldMesh)))
        //     {
        //         UISMTools::RemoveInstanceByTransform(OldInstanceHISMComponent, CropInfo.CropTransform);
        //     }
        //     else
        //     {
        //         UE_LOG(LogTemp, Log, TEXT("OldInstanceHISMComponent not found for %s"), *OldMesh->GetName());
        //         return;
        //     }
        // }
    }

    // 添加新实例（TODO: 迁移到 ISMManager->BatchAddInstances，保存返回的 FGlobalInstanceId）
    if (NewMesh)
    {
        // if (UInstancedStaticMeshComponent* NewISM = GetOrCreateCropHISMComponent(CropInfo))
        // {
        //     const int32 Index = NewISM->AddInstance(CropInfo.CropTransform);
        //     // 设置自定义数据（如病害、叶片等）
        //     NewISM->SetCustomDataValue(Index, CropCustomData::Disease, bWithers ? 1.f : 0.f);
        //     NewISM->SetCustomDataValue(Index, CropCustomData::IsLeaf, bWithers ? 1.f : 0.f);
        // }
    }
    if (NewGrowthStage == ECropGrowthStage::Mature)
    {
        FTimerHandle TimerHandle;
        GetWorld()->GetTimerManager().SetTimer(
            TimerHandle, // 定时器句柄
            [this, Coords]() // Lambda 捕获列表 (捕获 this 指针)
            {
                HarvestCrop(Coords);
            },
            FMath::RandRange(5.f, 10.f), // 延迟时间 (秒)
            false // 是否循环 (false=只执行一次, true=循环)
        );
    }
    if (NewGrowthStage == ECropGrowthStage::Harvested)
    {
        FTimerHandle TimerHandle;
        GetWorld()->GetTimerManager().SetTimer(
            TimerHandle, // 定时器句柄
            [this, Coords]() // Lambda 捕获列表 (捕获 this 指针)
            {
                RemoveCrop(Coords);
            },
            FMath::RandRange(5.f, 10.f), // 延迟时间 (秒)
            false // 是否循环 (false=只执行一次, true=循环)
        );
    }
}

// UInstancedStaticMeshComponent* UCropManagerComponent::GetOrCreateCropHISMComponent(const FPlotCropInfo& CropInfo)
// {
//     UStaticMesh* Mesh = nullptr;
//     if (!CropInfo.Crop)
//     {
//         UE_LOG(LogTemp, Error, TEXT("Crop is null"))
//         return nullptr;
//     }
//     switch (CropInfo.Crop->GetCropData().CropGrowthStage) {
//     case ECropGrowthStage::None:
//         Mesh = nullptr;
//         break;
//     case ECropGrowthStage::Seed:
//         Mesh = CropInfo.CropMeshVariant.SeedMesh;
//         break;
//     case ECropGrowthStage::Seedling:
//         Mesh = CropInfo.CropMeshVariant.SeedlingMesh;
//         break;
//     case ECropGrowthStage::Mature:
//         Mesh = CropInfo.CropMeshVariant.MatureMesh;
//         break;
//     case ECropGrowthStage::Harvested:
//         Mesh = CropInfo.CropMeshVariant.HarvestedMesh;
//         break;
//     }
//     if (Mesh == nullptr)
//     {
//         return nullptr;
//     }
//     if (CropHISMComponentMap.Contains(FStaticMeshKey(Mesh)))
//     {
//         return CropHISMComponentMap.FindChecked(FStaticMeshKey(Mesh));
//     }
//     UInstancedStaticMeshComponent* TargetComponent = NewObject<UInstancedStaticMeshComponent>(GetOwner());
//     TargetComponent->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);
//     TargetComponent->RegisterComponent();
//     TargetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
//     TargetComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
//     TargetComponent->SetStaticMesh(Mesh);
//     TargetComponent->SetNumCustomDataFloats(CropInfo.CropNumCustomDataFloats);
//     CropHISMComponentMap.Add(FStaticMeshKey(Mesh), TargetComponent);
//     return TargetComponent;
// }

void UCropManagerComponent::SetCropDisease(FIntPoint Coords, bool bDisease, bool bLeafs)
{
    if (!PlotCropMap.Contains(Coords)) return;
    FPlotCropInfo& CropInfo = PlotCropMap[Coords];
    if (!CropInfo.Crop)
    {
        UE_LOG(LogTemp, Error, TEXT("Plot is not planted"))
    }

    // TODO: 迁移到 ISMManager->UpdateInstanceCustomData（需传入已保存的 FGlobalInstanceId 与对应阶段 Mesh）
    if (bDisease)
    {
        // CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::Disease, 1.f);
        if (bLeafs)
        {
            // CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::IsLeaf, 1.f);
            CropInfo.Crop->GetCropDataRef().CropStateData.AddState(ECropState::NeedsWater);
        }
        else
        {
            // CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::IsLeaf, 0.f);
            CropInfo.Crop->GetCropDataRef().CropStateData.AddState(ECropState::NeedsWater);
        }
    }
    else
    {
        // CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::Disease, 0.f);
        // CropRenderer->SetCustomDataValue(CropIndex, CropCustomData::IsLeaf, 0.f);
        CropInfo.Crop->GetCropDataRef().CropStateData.RemoveState(ECropState::NeedsWater);
    }
	
}

void UCropManagerComponent::HandlePlotStateChanged(FIntPoint Coords, FPlotStateData NewPlotState)
{
    if (!PlotCropMap.Contains(Coords)) return;
    if (NewPlotState.IsEmpty())
    {
        RemoveCrop(Coords);
        return;
    }
    if (UCropBase* Crop = PlotCropMap[Coords].Crop)
    {
        Crop->HandlePlotStateChanged(NewPlotState);
    }
}

void UCropManagerComponent::HandlePlotMoistureChanged(FIntPoint Coords, float NewMoisture)
{
    if (!PlotCropMap.Contains(Coords)) return;
    if (UCropBase* Crop = PlotCropMap[Coords].Crop)
    {
        Crop->HandleCropMoisture(NewMoisture);
    }
}

void UCropManagerComponent::HandlePlotFertilityChanged(FIntPoint Coords, float NewFertility)
{
    if (!PlotCropMap.Contains(Coords)) return;
    if (UCropBase* Crop = PlotCropMap[Coords].Crop)
    {
        Crop->HandleCropFertility(NewFertility);
    }
}

void UCropManagerComponent::OnCropGrownThisTime(FIntPoint Coords, float MoistureCost, float FertilityCost) const
{
    if (!FarmlandComponent.IsValid()) return;
    FarmlandComponent->OnCropGrownThisTime(Coords, MoistureCost, FertilityCost);
}
