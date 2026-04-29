// FarmPrototype.cpp
#include "Mass/FarmPrototype.h"
#include "MassEntityManager.h"           // FMassEntityManager
#include "MassEntitySubsystem.h"         // UMassEntitySubsystem
#include "SmartObjectTypes.h"          // 解决 FSmartObjectSlot / FSmartObjectSlotHandle 核心依赖
#include "SmartObjectSubsystem.h"      // USmartObjectSubsystem 完整定义
#include "SmartObjectComponent.h"      // USmartObjectComponent 完整定义
#include "Subsystem/ISMPoolManager.h"    // 你的 ISM 对象池
#include "Engine/World.h"
#include "MassExecutionContext.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectRuntime.h"

// ---------------------------------------------------------------------------
// UFarmRenderSyncProcessor
// ---------------------------------------------------------------------------
UFarmRenderSyncProcessor::UFarmRenderSyncProcessor()
    : EntityQuery(*this)
{
    // 注册到默认处理阶段（PrePhysics / DuringPhysics 等自动调用）
    bAutoRegisterWithProcessingPhases = true;
    ExecutionFlags = static_cast<int32>(EProcessorExecutionFlags::All);
}

void UFarmRenderSyncProcessor::ConfigureQueries()
{
    EntityQuery.AddRequirement<FCropGrowthFragment>(EMassFragmentAccess::ReadOnly);
    EntityQuery.AddRequirement<FCropRenderStateFragment>(EMassFragmentAccess::ReadWrite);
}

void UFarmRenderSyncProcessor::Execute(FMassEntityManager& InEntityManager, FMassExecutionContext& Context)
{
    // 从世界获取 ISMPoolManager（也可在 BeginPlay 时缓存）
    if (!ISMPoolManager)
    {
        if (const UWorld* World = InEntityManager.GetWorld())
        {
            ISMPoolManager = World->GetSubsystem<UISMPoolManager>();
        }
        if (!ISMPoolManager) return;
    }

    EntityQuery.ForEachEntityChunk(InEntityManager, Context, [this](FMassExecutionContext& ChunkContext)
    {
        const int32 NumEntities = ChunkContext.GetNumEntities();
        const TConstArrayView<FCropGrowthFragment> Growths = ChunkContext.GetFragmentView<FCropGrowthFragment>();
        TArrayView<FCropRenderStateFragment> RenderStates = ChunkContext.GetMutableFragmentView<FCropRenderStateFragment>();

        for (int32 i = 0; i < NumEntities; ++i)
        {
            const int32 NewStage = Growths[i].GrowthStage;
            int32& CurrentVisible = RenderStates[i].CurrentVisibleStage;
            if (NewStage == CurrentVisible)
                continue;

            // 隐藏旧阶段
            if (CurrentVisible >= 0 && CurrentVisible < 4)
            {
                const int32 OldIndex = RenderStates[i].PerStageInstanceIndex[CurrentVisible];
                if (OldIndex != INDEX_NONE)
                {
                    // ---------- TODO: 对接你的 ISMPoolManager ----------
                    // 方案 A：如果你有直接操作 InstanceIndex 的接口
                    // ISMPoolManager->SetInstanceVisibleByIndex(OldIndex, false);

                    // 方案 B：通过 Transform 隐藏（缩小到 0）
                    FTransform HiddenTransform = FTransform(RenderStates[i].WorldRotation, RenderStates[i].WorldLocation, FVector::ZeroVector);
                    // ISMPoolManager->UpdateInstanceTransformByIndex(OldIndex, HiddenTransform);
                    UE_LOG(LogTemp, Warning, TEXT("[RenderSync] Hide instance index %d (stage %d)"), OldIndex, CurrentVisible);
                }
            }

            // 显示新阶段
            if (NewStage >= 0 && NewStage < 4)
            {
                const int32 NewIndex = RenderStates[i].PerStageInstanceIndex[NewStage];
                if (NewIndex != INDEX_NONE)
                {
                    FTransform ShowTransform = FTransform(RenderStates[i].WorldRotation, RenderStates[i].WorldLocation, FVector::OneVector);
                    // ISMPoolManager->UpdateInstanceTransformByIndex(NewIndex, ShowTransform);
                    UE_LOG(LogTemp, Warning, TEXT("[RenderSync] Show instance index %d (stage %d)"), NewIndex, NewStage);
                }
            }

            CurrentVisible = NewStage;
        }
    });
}

// ---------------------------------------------------------------------------
// AFarmPrototypeManager
// ---------------------------------------------------------------------------
AFarmPrototypeManager::AFarmPrototypeManager()
{
    PrimaryActorTick.bCanEverTick = true;
    SmartObjectComp = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObjectComp"));
}

void AFarmPrototypeManager::BeginPlay()
{
    Super::BeginPlay();

    SmartObjectSubsystem = GetWorld()->GetSubsystem<USmartObjectSubsystem>();
    ISMPoolManager = GetWorld()->GetSubsystem<UISMPoolManager>();

    if (UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>())
    {
        EntityManager = MassSubsystem->GetMutableEntityManager().AsShared();
    }

    if (EntityManager)
    {
        SetupMassEntities();
        RegisterSmartObjectSlots();
    }
}

void AFarmPrototypeManager::SetupMassEntities()
{
    // 定义原型：包含生长 + 渲染两个 Fragment
    TArray<const UScriptStruct*> FragmentTypes = {
        FCropGrowthFragment::StaticStruct(),
        FCropRenderStateFragment::StaticStruct()
    };
    CropArchetype = EntityManager->CreateArchetype(FragmentTypes, FMassArchetypeCreationParams());

    // 创建三个测试作物，沿 X 轴摆放，设为“成熟”阶段
    for (int32 i = 0; i < 3; ++i)
    {
        const FIntPoint Coord(i, 0);
        const FVector WorldLoc(Coord.X * GridSize, Coord.Y * GridSize, 0.0f);

        FMassEntityHandle Entity = EntityManager->CreateEntity(CropArchetype);
        CoordToEntity.Add(Coord, Entity);

        // 设置生长阶段：成熟
        FCropGrowthFragment& Growth = EntityManager->GetFragmentDataChecked<FCropGrowthFragment>(Entity);
        Growth.GrowthStage = 2;

        // 设置渲染状态（假设你已通过 ISMPoolManager 为四个阶段创建了实例，并记录了 Index）
        FCropRenderStateFragment& Render = EntityManager->GetFragmentDataChecked<FCropRenderStateFragment>(Entity);
        Render.WorldLocation = WorldLoc;
        Render.WorldRotation = FRotator::ZeroRotator;
        Render.CurrentVisibleStage = 2; // 成熟阶段可见
        Render.PerStageInstanceIndex.Init(-1, 4); // 初始化为 4 个 -1
        // 示例：假设每个阶段的实例 Index 依次为 0,1,2,3（实际需根据 ISMPoolManager 返回值填充）
        for (int32 Stage = 0; Stage < 4; ++Stage)
        {
            Render.PerStageInstanceIndex[Stage] = Stage; // 占位，实际需替换
        }
    }

    // 创建并持有处理器，它会自动注册
    // FarmRenderSyncProcessor = NewObject<UFarmRenderSyncProcessor>(this);
}

void AFarmPrototypeManager::RegisterSmartObjectSlots()
{
    if (!SmartObjectSubsystem || !EntityManager)
        return;

    // 需要一个预配置的 SmartObjectDefinition 资产（见下节）
    USmartObjectDefinition* Definition = HarvestDefinition.Get();
    if (!Definition)
    {
        UE_LOG(LogTemp, Error, TEXT("[FarmPrototype] HarvestDefinition is null. Please assign a SmartObjectDefinition asset."));
        return;
    }

    for (const auto& Pair : CoordToEntity)
    {
        const FIntPoint Coord = Pair.Key;
        const FVector WorldLoc(Coord.X * GridSize, Coord.Y * GridSize, 100.0f);

        // 构建世界变换
        FTransform SlotTransform(FRotator::ZeroRotator, WorldLoc);

        // 荷载数据（坐标）
        FCoordinatePayload Payload(Coord);
        
        // 创建智能对象运行时实例
        FSmartObjectHandle Handle = SmartObjectSubsystem->CreateSmartObject(
            *Definition,
            SlotTransform,
            FConstStructView::Make(Payload)
        );

        if (Handle.IsValid())
        {
            CoordToSmartObjectHandle.Add(Coord, Handle);
            HarvestSlots.Add(Coord);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[FarmPrototype] Failed to create smart object at (%d, %d)"), Coord.X, Coord.Y);
        }
    }
}

void AFarmPrototypeManager::HarvestCrop(FIntPoint Coord)
{
    if (!EntityManager)
        return;

    // 更新 Mass 数据
    FMassEntityHandle* Found = CoordToEntity.Find(Coord);
    if (Found)
    {
        FCropGrowthFragment* Growth = EntityManager->GetFragmentDataPtr<FCropGrowthFragment>(*Found);
        if (Growth && Growth->GrowthStage == 2)
        {
            Growth->GrowthStage = 3;
        }
    }

    // 移除智能对象
    if (FSmartObjectHandle* Handle = CoordToSmartObjectHandle.Find(Coord))
    {
        bool bDestroyed = SmartObjectSubsystem->DestroySmartObject(*Handle);
        ensure(bDestroyed);
        CoordToSmartObjectHandle.Remove(Coord);
        HarvestSlots.Remove(Coord);
    }
}