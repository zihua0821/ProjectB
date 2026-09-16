#include "Mass/CropGrowthProcessor.h"
#include "MassEntityManager.h"
#include "MassExecutionContext.h"
#include "Mass/CellMassFragments.h"
#include "Subsystem/GlobalConfigSubsystem.h"
#include "Farm/FarmlandConfigDataAsset.h"
#include "Crops/CropData.h"
#include "Engine/DataTable.h"

UCropGrowthProcessor::UCropGrowthProcessor()
{
    bAutoRegisterWithProcessingPhases = true;
}

void UCropGrowthProcessor::ConfigureQueries()
{
    EntityQuery.AddRequirement<FCellStaticFragment>(EMassFragmentAccess::ReadOnly);     
    EntityQuery.AddRequirement<FPlotStateFragment>(EMassFragmentAccess::ReadWrite);      
    EntityQuery.AddRequirement<FCropStaticConfigFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FCropStateFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FCropGrowthTimerFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FCropQualityFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.RegisterWithProcessor(*this);
}

void UCropGrowthProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    const UWorld* World = EntityManager.GetWorld();
    if (!World) return;

    const UGlobalConfigSubsystem* ConfigSub = World->GetSubsystem<UGlobalConfigSubsystem>();
    if (!ConfigSub) return;

    const UFarmlandConfigDataAsset* FarmConfig = ConfigSub->GetFarmlandConfig();
    const UDataTable* CropTable = ConfigSub->GetCropDataTable(); 
    if (!FarmConfig || !CropTable) return;

    const float DeltaTime = Context.GetDeltaTimeSeconds();

    EntityQuery.ForEachEntityChunk(EntityManager, Context, [&](FMassExecutionContext& InContext)
    {
        const int32 Num = InContext.GetNumEntities();
        const TConstArrayView<FCellStaticFragment>& CellStaticList = InContext.GetFragmentView<FCellStaticFragment>();
        const TArrayView<FPlotStateFragment> PlotStateList = InContext.GetMutableFragmentView<FPlotStateFragment>();
        const TConstArrayView<FCropStaticConfigFragment>& CropStaticConfigList = InContext.GetFragmentView<FCropStaticConfigFragment>();
        const TArrayView<FCropStateFragment> CropStateList = InContext.GetMutableFragmentView<FCropStateFragment>();
        const TArrayView<FCropGrowthTimerFragment> TimerList = InContext.GetMutableFragmentView<FCropGrowthTimerFragment>();
        const TArrayView<FCropQualityFragment> QualityList = InContext.GetMutableFragmentView<FCropQualityFragment>();

        for (int32 i = 0; i < Num; ++i)
        {
            const FCellStaticFragment& CellStatic = CellStaticList[i];
            FPlotStateFragment& PlotState = PlotStateList[i];
            FCropStateFragment& CropState = CropStateList[i];
            FCropGrowthTimerFragment& Timer = TimerList[i];
            FCropQualityFragment& Quality = QualityList[i];
            const FCropStaticConfigFragment& CropStaticConfig = CropStaticConfigList[i];

            if (CropState.CropGrowthStage == ECropGrowthStage::None)
            {
                const FMassEntityHandle Entity = InContext.GetEntity(i);
                InContext.Defer().RemoveFragment<FCropStaticConfigFragment>(Entity);
                InContext.Defer().RemoveFragment<FCropStateFragment>(Entity);
                InContext.Defer().RemoveFragment<FCropGrowthTimerFragment>(Entity);
                InContext.Defer().RemoveFragment<FCropQualityFragment>(Entity);
                continue;
            }
            // 1. 已收获不再处理
            if (CropState.CropGrowthStage == ECropGrowthStage::Harvested)
                continue;

            // 3. 种子阶段特殊处理：同时缺水缺肥 → 不更新、不生长
            const bool bNeedsWater = !PlotState.StateData.HasState(EPlotState::Watered);
            const bool bNeedsFertilizer = !PlotState.StateData.HasState(EPlotState::Fertilized);
            if (CropState.CropGrowthStage == ECropGrowthStage::Seed && bNeedsWater && bNeedsFertilizer)
                continue;

            // 4. 更新品质累加时间（所有未收获阶段）
            {
                FCropQualityData& Q = Quality.QualityData;
                if (CropState.CropStateData.HasState(ECropState::NeedsWater))         Q.AddWaterDeficitTime(DeltaTime);
                else if (CropState.CropStateData.HasState(ECropState::OverWatered))   Q.AddOverwaterTime(DeltaTime);
                else                                                                   Q.AddIdealMoistureTime(DeltaTime);

                if (CropState.CropStateData.HasState(ECropState::NeedsFertilizer))    Q.AddFertilizerDeficitTime(DeltaTime);
                else if (CropState.CropStateData.HasState(ECropState::NutrientBurn))  Q.AddOverfertilizeTime(DeltaTime);
                else                                                                   Q.AddIdealFertilityTime(DeltaTime);

                if (CropState.CropStateData.HasState(ECropState::WeedInfested))       Q.AddWeedInfestedTime(DeltaTime);
                else                                                                   Q.AddNoWeedTime(DeltaTime);

                if (CropState.CropStateData.HasState(ECropState::PestInfested))       Q.AddPestInfestedTime(DeltaTime);
                else                                                                   Q.AddNoPestTime(DeltaTime);
            }

            // 5. 成熟阶段：只更新品质，不生长、不消耗
            if (CropState.CropGrowthStage == ECropGrowthStage::Mature)
            {
                Quality.QualityData.GetQuality(Timer.TotalGrowthTime); // 刷新评分
                continue;
            }

            // 6. 计算生长速率
            float GrowthRate = 1.0f;
            if (bNeedsWater || bNeedsFertilizer)
            {
                GrowthRate = 0.0f; // 严重缺乏完全停止
            }
            else
            {
                if (CropState.CropStateData.HasState(ECropState::OverWatered))    GrowthRate *= FarmConfig->GrowthRate_OverWatered;
                if (CropState.CropStateData.HasState(ECropState::NutrientBurn))   GrowthRate *= FarmConfig->GrowthRate_NutrientBurn;
                if (CropState.CropStateData.HasState(ECropState::WeedInfested))   GrowthRate *= FarmConfig->GrowthRate_WeedInfested;
                if (CropState.CropStateData.HasState(ECropState::PestInfested))   GrowthRate *= FarmConfig->GrowthRate_PestInfested;
            }

            // 7. 生长计时
            if (GrowthRate > 0.0f)
            {
                Timer.TotalGrowthTime += DeltaTime * GrowthRate;
                switch (CropState.CropGrowthStage)
                {
                case ECropGrowthStage::Seed:
                    Timer.CurrentStageGrowthTime = Timer.TotalGrowthTime;
                    break;
                case ECropGrowthStage::Seedling:
                    Timer.CurrentStageGrowthTime = Timer.TotalGrowthTime - CropStaticConfig.GrowthSeedTime;
                    break;
                default: break;
                }

                // 8. 阶段转换
                if (CropState.CropGrowthStage == ECropGrowthStage::Seed && Timer.TotalGrowthTime >= CropStaticConfig.GrowthSeedTime)
                {
                    CropState.CropGrowthStage = ECropGrowthStage::Seedling;
                    // 品质刷新（发生阶段性事件）
                    Quality.QualityData.GetQuality(Timer.TotalGrowthTime);
                }
                else if (CropState.CropGrowthStage == ECropGrowthStage::Seedling && Timer.TotalGrowthTime >= CropStaticConfig.GrowthSeedTime + CropStaticConfig.GrowthSeedlingTime)
                {
                    CropState.CropGrowthStage = ECropGrowthStage::Mature;
                    CropState.CropStateData.AddState(ECropState::ReadyForHarvest);
                    Quality.QualityData.GetQuality(Timer.TotalGrowthTime);

                    // 推迟到游戏线程执行，通知管理器
                    Context.Defer().PushCommand<FMassDeferredSetCommand>(
                        [Coords = CellStatic.Coords, CellStatic](FMassEntityManager& EM)
                        {
                            if (const UWorld* World = EM.GetWorld())
                            {
                                if (UGlobalConfigSubsystem* Config = World->GetSubsystem<UGlobalConfigSubsystem>())
                                {
                                    Config->MatureCropQueue.Enqueue(CellStatic.Coords);
                                }
                            }
                        });
                }

                // 9. 消耗地块资源
                // PlotState.MoisturePercentage = FMath::Clamp(PlotState.MoisturePercentage - CropStaticConfig.MoistureConsumePerSecond * DeltaTime, 0.0f, 1.0f);
                // PlotState.FertilityPercentage = FMath::Clamp(PlotState.FertilityPercentage - CropStaticConfig.FertilityConsumePerSecond * DeltaTime, 0.0f, 1.0f);

                // 注意：如果水分或肥力过低，应触发移除 Watered/Fertilized 状态？暂由自然流失处理器统一处理
            }
            else
            {
                // 即使不生长，品质时间也已累加，更新评分
                Quality.QualityData.GetQuality(Timer.TotalGrowthTime);
            }
        }
    });
}