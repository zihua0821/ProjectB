// FarmlandActor.cpp
#include "Farm/FarmlandActor.h"
#include "Component/IncrementalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Subsystem/ISMManager.h"

AFarmlandActor::AFarmlandActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.0f;

    RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
    SetRootComponent(RootSceneComponent);
}

void AFarmlandActor::BeginPlay()
{
    Super::BeginPlay();

    if (!TestMesh)
    {
        UE_LOG(LogTemp, Error, TEXT("FarmlandActor: TestMesh is not set! Tests will fail."));
        return;
    }
    UISMManager* ISMManager = GetWorld()->GetSubsystem<UISMManager>();
    if (!ISMManager) return;

    FISMComponentParams TestParams;
    TestParams.AttachParent = RootSceneComponent;
    TestParams.bUseIncrementalISM = true;
    ISMManager->GetOrCreateISMComponent(TestMesh, 3, TestParams);

    if (TestMeshB)
    {
        ISMManager->GetOrCreateISMComponent(TestMeshB, 3, TestParams);
    }

    ISMManager->PreAllocateSlots(TestMesh, TotalInstanceCount);

    TArray<FTransform> NewTransforms;
    NewTransforms.Reserve(TotalInstanceCount);
    for (int32 i = 0; i < TotalInstanceCount; ++i)
    {
        FVector Location(
            FMath::RandRange(-5000.0f, 5000.0f),
            FMath::RandRange(-5000.0f, 5000.0f),
            0.0f);
        NewTransforms.Emplace(FQuat::Identity, Location, FVector::One());
    }
    InstanceWorldTransforms = NewTransforms;

    UE_LOG(LogTemp, Warning, TEXT("[FarmlandActor] Ready."));
}

void AFarmlandActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    switch (CurrentTestLevel)
    {
    case 0: break;
    case 1:
        TestLevel1_BatchPressure_Tick(DeltaTime);
        break;
    case 2:
        TestLevel2_CrossSwitch_Tick(DeltaTime);
        break;
    case 3: break;
    case 4:
        TestLevel4_SteadyState_Tick(DeltaTime);
        break;
    default:
        break;
    }
}

void AFarmlandActor::CleanupTest()
{
    UISMManager* Mgr = GetWorld()->GetSubsystem<UISMManager>();
    if (!Mgr) return;

    for (auto& Id : GlobalInstanceIds)  { if (Id.IsValid()) Mgr->ReleaseSlot(TestMesh, Id); }
    for (auto& Id : TestIdsA)           { if (Id.IsValid()) Mgr->ReleaseSlot(TestMesh, Id); }
    for (auto& Id : TestIdsB)           { if (TestMeshB && Id.IsValid()) Mgr->ReleaseSlot(TestMeshB, Id); }
    for (int32 i = 0; i < Level4InstanceIds.Num(); ++i)
    {
        if (Level4InstanceIds[i].IsValid() && TestMeshArray.IsValidIndex(Level4MeshIndices[i]))
            Mgr->ReleaseSlot(TestMeshArray[Level4MeshIndices[i]], Level4InstanceIds[i]);
    }

    GlobalInstanceIds.Empty();
    TestIdsA.Empty();
    TestIdsB.Empty();
    Level4InstanceIds.Empty();
    Level4MeshIndices.Empty();
    Level4Transforms.Empty();

    CurrentTestLevel = -1;
    TestPhase = 0;
    TestProgress = 0;
    TestStartTime = 0.0;
    TestPhaseStartTime = 0.0;

    Mgr->FlushAllIncrementalComponents();
}

// ============================================================================
// 对外调用入口
// ============================================================================

void AFarmlandActor::RunTest_Level0_SingleInstance()
{
    CleanupTest();
    CurrentTestLevel = 0;
    Internal_Level0_SingleInstance();
    CurrentTestLevel = -1;
}

void AFarmlandActor::RunTest_Level1_BatchPressure(int32 Count)
{
    CleanupTest();
    CurrentTestLevel = 1;
    Internal_Level1_BatchPressure_Begin(Count);
}

void AFarmlandActor::RunTest_Level2_CrossSwitch(int32 Count)
{
    CleanupTest();
    CurrentTestLevel = 2;
    Internal_Level2_CrossSwitch_Begin(Count);
}

void AFarmlandActor::RunTest_Level3_FullPressure()
{
    CleanupTest();
    CurrentTestLevel = 3;
    Internal_Level3_FullPressure();
    CurrentTestLevel = -1;
}

void AFarmlandActor::RunTest_Level4_SteadyState()
{
    if (TestMeshArray.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[Level 4] TestMeshArray is empty! Aborting."));
        return;
    }
    CleanupTest();
    CurrentTestLevel = 4;
    Internal_Level4_SteadyState_Begin();
}

// ============================================================================
// 第0级：单实例功能验证（单帧完成）
// ============================================================================
void AFarmlandActor::Internal_Level0_SingleInstance()
{
    UE_LOG(LogTemp, Warning, TEXT("========== [Test Level 0] Single Instance =========="));

    UISMManager* Mgr = GetWorld()->GetSubsystem<UISMManager>();
    if (!Mgr) return;

    Mgr->PreAllocateSlots(TestMesh, 5);

    // Step 1: Acquire
    FTransform T1(FVector(0.f, 0.f, 0.f));
    FGlobalInstanceId Id1 = Mgr->AcquireSlot(TestMesh, T1, {1.0f, 0.0f, 0.0f});
    check(Id1.IsValid());
    UE_LOG(LogTemp, Warning, TEXT("  [Step 1] Acquired: bucket=%d, localId.Id=%d"),
        Id1.BucketIndex, Id1.LocalId.Id);
    GlobalInstanceIds.Add(Id1);
    Mgr->FlushAllIncrementalComponents();

    // Step 2: Release → Re-Acquire（检测池复用）
    Mgr->ReleaseSlot(TestMesh, Id1);
    GlobalInstanceIds.Empty();
    Mgr->FlushAllIncrementalComponents();

    FTransform T2(FVector(100.f, 0.f, 0.f));
    FGlobalInstanceId Id2 = Mgr->AcquireSlot(TestMesh, T2, {1.0f, 1.0f, 0.0f});
    check(Id2.IsValid());
    UE_LOG(LogTemp, Warning, TEXT("  [Step 2] Re-acquired: bucket=%d, localId.Id=%d"),
        Id2.BucketIndex, Id2.LocalId.Id);
    GlobalInstanceIds.Add(Id2);
    Mgr->FlushAllIncrementalComponents();

    // Step 3: 变更 CustomData
    Mgr->UpdateInstanceCustomData(TestMesh, Id2, 1, 0.5f);
    Mgr->FlushAllIncrementalComponents();
    UE_LOG(LogTemp, Warning, TEXT("  [Step 3] Updated custom data (index=1, value=0.5)"));

    UE_LOG(LogTemp, Warning, TEXT("========== Level 0 Complete =========="));
}

// ============================================================================
// 第1级：单Mesh批量压力（多帧完成）
// ============================================================================
void AFarmlandActor::Internal_Level1_BatchPressure_Begin(int32 Count)
{
    UE_LOG(LogTemp, Warning, TEXT("========== [Test Level 1] Batch Pressure: count=%d =========="), Count);

    UISMManager* Mgr = GetWorld()->GetSubsystem<UISMManager>();
    if (!Mgr) return;

    TestPhase = 0;
    TestCount = Count;
    TestMaxCount = Count;
    TestProgress = 0;
    TestStartTime = FPlatformTime::Seconds();
    TestPhaseStartTime = TestStartTime;

    Mgr->PreAllocateSlots(TestMesh, Count);
    GlobalInstanceIds.Reserve(Count);

    UE_LOG(LogTemp, Warning, TEXT("  Phase 0: Acquire %d instances..."), Count);
}

void AFarmlandActor::TestLevel1_BatchPressure_Tick(float DeltaTime)
{
    UISMManager* Mgr = GetWorld()->GetSubsystem<UISMManager>();
    if (!Mgr) return;

    const int32 BatchSize = 10000;
    int32 End = FMath::Min(TestProgress + BatchSize, TestMaxCount);

    if (TestPhase == 0)
    {
        for (int32 i = TestProgress; i < End; ++i)
        {
            FTransform T(FVector(i * 10.f, 0.f, 0.f));
            GlobalInstanceIds.Add(Mgr->AcquireSlot(TestMesh, T, {1.0f, 0.0f, 0.0f}));
        }
        TestProgress = End;

        if (TestProgress >= TestMaxCount)
        {
            Mgr->FlushAllIncrementalComponents();
            double Now = FPlatformTime::Seconds();
            UE_LOG(LogTemp, Warning, TEXT("  [Phase 0] Acquire %d done: %.3fms (%.4fms/op)"),
                TestMaxCount, (Now - TestPhaseStartTime) * 1000.0,
                (Now - TestPhaseStartTime) * 1000.0 / TestMaxCount);

            TestPhase = 1;
            TestProgress = 0;
            TestPhaseStartTime = Now;
            UE_LOG(LogTemp, Warning, TEXT("  Phase 1: Release %d instances..."), TestMaxCount);
        }
    }
    else if (TestPhase == 1)
    {
        for (int32 i = TestProgress; i < End; ++i)
        {
            Mgr->ReleaseSlot(TestMesh, GlobalInstanceIds[i]);
        }
        TestProgress = End;

        if (TestProgress >= TestMaxCount)
        {
            Mgr->FlushAllIncrementalComponents();
            double Now = FPlatformTime::Seconds();
            UE_LOG(LogTemp, Warning, TEXT("  [Phase 1] Release %d done: %.3fms (%.4fms/op)"),
                TestMaxCount, (Now - TestPhaseStartTime) * 1000.0,
                (Now - TestPhaseStartTime) * 1000.0 / TestMaxCount);

            double Total = (Now - TestStartTime) * 1000.0;
            UE_LOG(LogTemp, Warning, TEXT("========== Level 1 Complete: total=%.3fms =========="), Total);

            GlobalInstanceIds.Empty();
            CurrentTestLevel = -1;
        }
    }
}

// ============================================================================
// 第2级：跨Mesh切换（多帧完成）
// ============================================================================
void AFarmlandActor::Internal_Level2_CrossSwitch_Begin(int32 Count)
{
    if (!TestMeshB)
    {
        UE_LOG(LogTemp, Error, TEXT("[Level 2] TestMeshB is not set! Aborting."));
        CurrentTestLevel = -1;
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("========== [Test Level 2] Cross-Mesh Switch: count=%d =========="), Count);

    UISMManager* Mgr = GetWorld()->GetSubsystem<UISMManager>();
    if (!Mgr) return;

    Mgr->PreAllocateSlots(TestMesh, Count);
    Mgr->PreAllocateSlots(TestMeshB, Count);

    TestPhase = 0;
    TestCount = Count;
    TestMaxCount = Count;
    TestProgress = 0;
    TestStartTime = FPlatformTime::Seconds();
    TestPhaseStartTime = TestStartTime;

    TestIdsA.Reserve(Count);

    UE_LOG(LogTemp, Warning, TEXT("  Phase 0: Acquire MeshA × %d..."), Count);
}

void AFarmlandActor::TestLevel2_CrossSwitch_Tick(float DeltaTime)
{
    UISMManager* Mgr = GetWorld()->GetSubsystem<UISMManager>();
    if (!Mgr) return;

    const int32 BatchSize = 10000;
    int32 End = FMath::Min(TestProgress + BatchSize, TestMaxCount);

    if (TestPhase == 0)
    {
        for (int32 i = TestProgress; i < End; ++i)
        {
            FTransform T(FVector(i * 10.f, 0.f, 50.f));
            TestIdsA.Add(Mgr->AcquireSlot(TestMesh, T, {1.0f, 0.0f, 0.0f}));
        }
        TestProgress = End;

        if (TestProgress >= TestMaxCount)
        {
            Mgr->FlushAllIncrementalComponents();
            double Now = FPlatformTime::Seconds();
            UE_LOG(LogTemp, Warning, TEXT("  [Phase 0] Acquire MeshA × %d: %.3fms"),
                TestMaxCount, (Now - TestPhaseStartTime) * 1000.0);

            TestPhase = 1;
            TestProgress = 0;
            TestPhaseStartTime = Now;
            UE_LOG(LogTemp, Warning, TEXT("  Phase 1: Release MeshA + Acquire MeshB × %d..."), TestMaxCount);
        }
    }
    else if (TestPhase == 1)
    {
        for (int32 i = TestProgress; i < End; ++i)
        {
            Mgr->ReleaseSlot(TestMesh, TestIdsA[i]);
            FTransform T(FVector(i * 10.f, 200.f, 50.f));
            TestIdsA[i] = Mgr->AcquireSlot(TestMeshB, T, {1.0f, 0.0f, 0.0f});
        }
        TestProgress = End;

        if (TestProgress >= TestMaxCount)
        {
            Mgr->FlushAllIncrementalComponents();
            double Now = FPlatformTime::Seconds();
            UE_LOG(LogTemp, Warning, TEXT("  [Phase 1] Switch MeshA→MeshB × %d: %.3fms (%.4fms/op)"),
                TestMaxCount, (Now - TestPhaseStartTime) * 1000.0,
                (Now - TestPhaseStartTime) * 1000.0 / TestMaxCount);

            double Total = (Now - TestStartTime) * 1000.0;
            UE_LOG(LogTemp, Warning, TEXT("========== Level 2 Complete: total=%.3fms =========="), Total);

            for (int32 i = 0; i < TestMaxCount; ++i)
            {
                Mgr->ReleaseSlot(TestMeshB, TestIdsA[i]);
            }
            Mgr->FlushAllIncrementalComponents();
            TestIdsA.Empty();
            CurrentTestLevel = -1;
        }
    }
}

// ============================================================================
// 第3级：原始全量压力（单帧完成）
// ============================================================================
void AFarmlandActor::Internal_Level3_FullPressure()
{
    UE_LOG(LogTemp, Warning, TEXT("========== [Test Level 3] Full Pressure: %d instances =========="), TotalInstanceCount);

    UISMManager* Mgr = GetWorld()->GetSubsystem<UISMManager>();
    if (!Mgr) return;

    double T0 = FPlatformTime::Seconds();
    
    GlobalInstanceIds.Reserve(TotalInstanceCount);
    for (int32 i = 0; i < InstanceWorldTransforms.Num(); ++i)
    {
        GlobalInstanceIds.Add(Mgr->AcquireSlot(TestMesh, InstanceWorldTransforms[i], {1.0f, 0.0f, 0.0f}));
    }

    Mgr->FlushAllIncrementalComponents();
    double T1 = FPlatformTime::Seconds();

    UE_LOG(LogTemp, Warning, TEXT("  Acquire %d instances: %.3fms (%.4fms/op)"),
        TotalInstanceCount, (T1 - T0) * 1000.0, (T1 - T0) * 1000.0 / TotalInstanceCount);

    double T2 = FPlatformTime::Seconds();
    for (auto& Id : GlobalInstanceIds)
    {
        Mgr->ReleaseSlot(TestMesh, Id);
    }
    GlobalInstanceIds.Empty();
    Mgr->FlushAllIncrementalComponents();
    double T3 = FPlatformTime::Seconds();

    UE_LOG(LogTemp, Warning, TEXT("  Release %d instances: %.3fms (%.4fms/op)"),
        TotalInstanceCount, (T3 - T2) * 1000.0, (T3 - T2) * 1000.0 / TotalInstanceCount);

    UE_LOG(LogTemp, Warning, TEXT("========== Level 3 Complete: total=%.3fms =========="), (T3 - T0) * 1000.0);
}

// ============================================================================
// 第4级：稳态压力测试 — 模拟真实作物生长切换（持续多帧）
// ============================================================================
void AFarmlandActor::Internal_Level4_SteadyState_Begin()
{
    UE_LOG(LogTemp, Warning, TEXT("========== [Test Level 4] Steady State =========="));
    UE_LOG(LogTemp, Warning, TEXT("  Meshes: %d | Total: %d | Changes/Frame: %d"),
        TestMeshArray.Num(), Level4TotalInstances, Level4ChangesPerFrame);

    UISMManager* Mgr = GetWorld()->GetSubsystem<UISMManager>();
    if (!Mgr) return;

    // 1. 为每个 Mesh 预创建 ISM 组件并预分配槽位
    FISMComponentParams Params;
    Params.AttachParent = RootSceneComponent;
    Params.bUseIncrementalISM = true;
    for (UStaticMesh* Mesh : TestMeshArray)
    {
        if (!Mesh) continue;
        Mgr->GetOrCreateISMComponent(Mesh, 3, Params);
        Mgr->PreAllocateSlots(Mesh, Level4TotalInstances);
    }

    // 2. Phase 0：一次性 Acquire 全部实例（随机 Mesh + 随机位置）
    Level4InstanceIds.Reserve(Level4TotalInstances);
    Level4MeshIndices.Reserve(Level4TotalInstances);
    Level4Transforms.Reserve(Level4TotalInstances);

    double T0 = FPlatformTime::Seconds();
    for (int32 i = 0; i < Level4TotalInstances; ++i)
    {
        const int32 MeshIdx = FMath::RandRange(0, TestMeshArray.Num() - 1);
        UStaticMesh* Mesh = TestMeshArray[MeshIdx];

        FTransform T(
            FQuat::Identity,
            FVector(
                FMath::RandRange(-5000.0f, 5000.0f),
                FMath::RandRange(-5000.0f, 5000.0f),
                0.0f),
            FVector::OneVector);

        FGlobalInstanceId Id = Mgr->AcquireSlot(Mesh, T, {1.0f, 0.0f, 0.0f});
        check(Id.IsValid());

        Level4InstanceIds.Add(Id);
        Level4MeshIndices.Add(MeshIdx);
        Level4Transforms.Add(T);
    }
    Mgr->FlushAllIncrementalComponents();
    double T1 = FPlatformTime::Seconds();

    UE_LOG(LogTemp, Warning, TEXT("  [Phase 0] Acquire %d done: %.3fms (%.4fms/op)"),
        Level4TotalInstances, (T1 - T0) * 1000.0, (T1 - T0) * 1000.0 / Level4TotalInstances);

    // 3. 进入 Phase 1：每帧稳态切换
    TestPhase = 1;
    TestStartTime = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Warning, TEXT("  [Phase 1] Steady state: %d swaps/frame. Log every 60 frames."), Level4ChangesPerFrame);
}

void AFarmlandActor::TestLevel4_SteadyState_Tick(float DeltaTime)
{
    if (TestPhase != 1) return;

    UISMManager* Mgr = GetWorld()->GetSubsystem<UISMManager>();
    if (!Mgr) return;

    const int32 SwapCount = FMath::Min(Level4ChangesPerFrame, Level4InstanceIds.Num());
    for (int32 s = 0; s < SwapCount; ++s)
    {
        const int32 Idx = FMath::RandRange(0, Level4InstanceIds.Num() - 1);
        const int32 OldMeshIdx = Level4MeshIndices[Idx];
        const int32 NewMeshIdx = FMath::RandRange(0, TestMeshArray.Num() - 1);

        UStaticMesh* OldMesh = TestMeshArray[OldMeshIdx];
        UStaticMesh* NewMesh = TestMeshArray[NewMeshIdx];

        // Release 旧 Mesh
        Mgr->ReleaseSlot(OldMesh, Level4InstanceIds[Idx]);

        // 随机微移位置（模拟作物生长时视觉变化）
        FTransform& CurT = Level4Transforms[Idx];
        const FVector Delta(
            FMath::RandRange(-5.0f, 5.0f),
            FMath::RandRange(-5.0f, 5.0f),
            0.0f);
        CurT.SetLocation(CurT.GetLocation() + Delta);

        // Acquire 新 Mesh
        Level4InstanceIds[Idx] = Mgr->AcquireSlot(NewMesh, CurT, {1.0f, 0.0f, 0.0f});
        Level4MeshIndices[Idx] = NewMeshIdx;
    }

    Mgr->FlushAllIncrementalComponents();
}