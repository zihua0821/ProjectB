#include "Subsystem/ISMManager.h"
#include "Components/InstancedStaticMeshComponent.h"

void UISMManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UISMManager::Deinitialize()
{
    MeshToISM.Empty();
    Super::Deinitialize();
}

UInstancedStaticMeshComponent* UISMManager::CreateISMComponent(UStaticMesh* Mesh, const int32 NumCustomDataFloats, const FISMComponentParams& Params)
{
    if (!Mesh || !Params.AttachParent)
    {
        UE_LOG(LogTemp, Warning, TEXT("UISMManager::CreateISMComponent - Invalid Mesh or AttachParent"));
        return nullptr;
    }

    AActor* Owner = Params.AttachParent->GetOwner();
    if (!Owner) return nullptr;

    UInstancedStaticMeshComponent* NewISM = NewObject<UInstancedStaticMeshComponent>(Owner, NAME_None, RF_Transient);
    NewISM->SetStaticMesh(Mesh);
    NewISM->SetNumCustomDataFloats(NumCustomDataFloats);
    NewISM->SetCollisionEnabled(Params.CollisionEnabled);
    NewISM->SetCollisionObjectType(Params.CollisionObjectType);
    NewISM->SetCollisionResponseToAllChannels(Params.CollisionResponseToAllChannels);
    NewISM->SetCanEverAffectNavigation(Params.bCanEverAffectNavigation);
    NewISM->bDisallowNanite = Params.bDisallowNanite;
    NewISM->SetCastShadow(Params.bCastShadow);

    if (Params.MinDrawDistance > 0.0f || Params.MaxDrawDistance > 0.0f)
    {
        NewISM->SetCullDistances(Params.MinDrawDistance, Params.MaxDrawDistance);
    }

    NewISM->SetupAttachment(Params.AttachParent);
    NewISM->RegisterComponent();
    return NewISM;
}

UInstancedStaticMeshComponent* UISMManager::GetOrCreateISMComponent(UStaticMesh* Mesh, const int32 NumCustomDataFloats, const FISMComponentParams& Params)
{
    if (!Mesh) return nullptr;

    const FISMMeshKey Key(Mesh);
    if (TObjectPtr<UInstancedStaticMeshComponent>* Existing = MeshToISM.Find(Key))
    {
        return *Existing;
    }

    UInstancedStaticMeshComponent* NewISM = CreateISMComponent(Mesh, NumCustomDataFloats, Params);
    if (NewISM)
    {
        MeshToISM.Add(Key, NewISM);
    }
    return NewISM;
}

UInstancedStaticMeshComponent* UISMManager::GetISMComponent(UStaticMesh* Mesh) const
{
    if (!Mesh) return nullptr;
    const FISMMeshKey Key(Mesh);
    if (const TObjectPtr<UInstancedStaticMeshComponent>* Found = MeshToISM.Find(Key))
    {
        return *Found;
    }
    return nullptr;
}

void UISMManager::UpdateInstanceCustomData(UStaticMesh* Mesh, const int32 InstanceIndex, const int32 CustomDataIndex, const float Value) const
{
    TArray<FPrimitiveInstanceId> InstanceIds;
    UInstancedStaticMeshComponent* ISMComp = GetISMComponent(Mesh);
    if (!ISMComp) return;
    ISMComp->SetCustomDataValue(InstanceIndex, CustomDataIndex, Value);
}

TArray<FPrimitiveInstanceId> UISMManager::BatchAddInstances(
    UStaticMesh* Mesh,
    const TArray<FTransform>& Transforms,
    const int32 NumCustomDataFloats,
    const FISMComponentParams& ISMParams,
    const TArray<float>& CustomData)
{
    TArray<FPrimitiveInstanceId> InstanceIds;
    UInstancedStaticMeshComponent* ISMComp = GetOrCreateISMComponent(Mesh, NumCustomDataFloats, ISMParams);
    if (!ISMComp || Transforms.Num() == 0) return InstanceIds;

    InstanceIds = ISMComp->AddInstancesById(Transforms, true);

    if (CustomData.Num() > 0)
    {
        const int32 ExpectedCount = InstanceIds.Num() * NumCustomDataFloats;
        if (CustomData.Num() == ExpectedCount)
        {
            ISMComp->SetCustomDataById(InstanceIds, CustomData);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("UISMManager::BatchAddInstances - CustomData size mismatch. Expected %d, got %d"),
                ExpectedCount, CustomData.Num());
        }
    }

    return InstanceIds;
}

void UISMManager::BatchRemoveInstances(
    UStaticMesh* Mesh,
    const TArray<FPrimitiveInstanceId>& InstanceIds) const
{
    UInstancedStaticMeshComponent* ISMComp = GetISMComponent(Mesh);
    if (!ISMComp || InstanceIds.Num() == 0) return;
    ISMComp->RemoveInstancesById(InstanceIds);
}

void UISMManager::BatchUpdateInstanceTransforms(
    UStaticMesh* Mesh,
    const TArray<FPrimitiveInstanceId>& InstanceIds,
    const TArray<FTransform>& Transforms) const
{
    UInstancedStaticMeshComponent* ISMComp = GetISMComponent(Mesh);
    if (!ISMComp || InstanceIds.Num() == 0) return;

    check(InstanceIds.Num() == Transforms.Num());

    for (int32 i = 0; i < InstanceIds.Num(); ++i)
    {
        ISMComp->UpdateInstanceTransformById(InstanceIds[i], Transforms[i]);
    }
}

void UISMManager::BatchSetInstanceCustomData(
    UStaticMesh* Mesh,
    const TArray<FPrimitiveInstanceId>& InstanceIds,
    const TArray<float>& CustomData) const
{
    UInstancedStaticMeshComponent* ISMComp = GetISMComponent(Mesh);
    if (!ISMComp || InstanceIds.Num() == 0 || CustomData.Num() == 0) return;
    ISMComp->SetCustomDataById(InstanceIds, CustomData);
}

bool UISMManager::LineTraceISMInstances(UStaticMesh* Mesh, FHitResult& OutHit, const FVector& Start, const FVector& End,
    const FCollisionQueryParams& Params) const
{
    UInstancedStaticMeshComponent* ISMComp = GetISMComponent(Mesh);
    if (!ISMComp) return false;
    return ISMComp && ISMComp->LineTraceComponent(OutHit, Start, End, Params);
}
