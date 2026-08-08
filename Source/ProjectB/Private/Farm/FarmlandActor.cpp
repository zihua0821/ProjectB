// FarmlandActor.cpp
// 作物压力场景性能测试实现
#include "Farm/FarmlandActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

AFarmlandActor::AFarmlandActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	SetRootComponent(RootComponent);
}

UStaticMesh* AFarmlandActor::GetVariantStageMesh(const FCropMeshVariant& Variant, int32 Stage)
{
	switch (Stage)
	{
	case 0: return Variant.SeedMesh;
	case 1: return Variant.SeedlingMesh;
	case 2: return Variant.MatureMesh;
	case 3: return Variant.HarvestedMesh;
	}
	return nullptr;
}

int32 AFarmlandActor::PickRandomStage(const FCropRowCache& Row, int32 VariantIndex, int32 ExcludeStage) const
{
	const FCropMeshVariant& Variant = Row.Variants[VariantIndex];

	int32 Candidates[4];
	int32 NumCandidates = 0;
	for (int32 s = 0; s < 4; ++s)
	{
		if (s != ExcludeStage && GetVariantStageMesh(Variant, s))
		{
			Candidates[NumCandidates++] = s;
		}
	}
	if (NumCandidates == 0)
	{
		return ExcludeStage;
	}
	return Candidates[FMath::RandRange(0, NumCandidates - 1)];
}

void AFarmlandActor::BeginPlay()
{
	Super::BeginPlay();

	if (!CropDataTable)
	{
		UE_LOG(LogTemp, Error, TEXT("[CropStress] CropDataTable 未设置（请在关卡/蓝图指定作物数据表）。"));
		return;
	}

	FRandomStream Rand(RandomSeed);

	// ① 解析数据表：收集所有行的作物配置（与 PlantCrop 的 FCropTableRow 同源）
	for (const TPair<FName, uint8*>& Entry : CropDataTable->GetRowMap())
	{
		const FCropTableRow* Row = reinterpret_cast<const FCropTableRow*>(Entry.Value);
		if (!Row || Row->MeshVariants.Num() == 0)
		{
			continue;
		}
		FCropRowCache& Cache = RowCaches.AddDefaulted_GetRef();
		Cache.CropID = Row->CropID;
		Cache.NumCustomDataFloats = FMath::Max(1, Row->CropNumCustomDataFloats);
		Cache.Variants = Row->MeshVariants;
	}
	if (RowCaches.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[CropStress] 数据表无有效行（需 FCropTableRow 且 MeshVariants 非空）。"));
		return;
	}

	// ② 收集唯一网格体（行 × 变体 × 4 阶段），排序保证确定性（TSet 无序）
	TSet<UStaticMesh*> UniqueMeshes;
	for (const FCropRowCache& Row : RowCaches)
	{
		for (const FCropMeshVariant& Variant : Row.Variants)
		{
			for (int32 s = 0; s < 4; ++s)
			{
				if (UStaticMesh* Mesh = GetVariantStageMesh(Variant, s))
				{
					UniqueMeshes.Add(Mesh);
				}
			}
		}
	}
	TArray<UStaticMesh*> UniqueMeshArray = UniqueMeshes.Array();
	UniqueMeshArray.Sort([](const UStaticMesh& A, const UStaticMesh& B) { return A.GetPathName() < B.GetPathName(); });

	// 分桶：地图切 BucketsPerAxis² 个空间区块；组件 = 网格体 × 区块（空组件不绘制，切换目标永远存在）
	const int32 BucketsPerAxisClamped = FMath::Max(1, BucketsPerAxis);
	NumBuckets = BucketsPerAxisClamped * BucketsPerAxisClamped;
	Components.Reserve(UniqueMeshArray.Num() * NumBuckets);
	for (int32 m = 0; m < UniqueMeshArray.Num(); ++m)
	{
		UStaticMesh* Mesh = UniqueMeshArray[m];
		MeshToGlobalIdx.Add(Mesh, m);

		// 该网格体所属行的 CustomData 数量（同网格体跨行时取先注册行的值）
		int32 NumFloats = 2;
		for (const FCropRowCache& Row : RowCaches)
		{
			bool bFound = false;
			for (const FCropMeshVariant& Variant : Row.Variants)
			{
				for (int32 s = 0; s < 4; ++s)
				{
					if (GetVariantStageMesh(Variant, s) == Mesh)
					{
						bFound = true;
						break;
					}
				}
				if (bFound)
				{
					break;
				}
			}
			if (bFound)
			{
				NumFloats = Row.NumCustomDataFloats;
				break;
			}
		}

		for (int32 b = 0; b < NumBuckets; ++b)
		{
			FStressComponent& SC = Components.AddDefaulted_GetRef();
			SC.Mesh = Mesh;
			SC.NumCustomDataFloats = NumFloats;
			SC.Comp = NewObject<UInstancedStaticMeshComponent>(this);
			SC.Comp->SetStaticMesh(Mesh);
			SC.Comp->SetNumCustomDataFloats(NumFloats);
			SC.Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SC.Comp->SetCanEverAffectNavigation(false);
			SC.Comp->SetCastShadow(true);
			SC.Comp->SetupAttachment(RootComponent);
			SC.Comp->RegisterComponent();
			SC.Comp->MarkRenderStateDirty(); // 仅首次创建需要全量
		}
	}

	// ③ 分配实例：随机作物 × 变体 × 初始阶段，网格排布（与真实"确定随机相位"一致，避免同步尖峰）
	Cols = FMath::CeilToInt(FMath::Sqrt((float)TotalInstances));
	const FVector OriginOffset(-Cols * Spacing * 0.5f, -Cols * Spacing * 0.5f, 0.0f);
	MapSize = Cols * Spacing;

	// 区块划分（每边 BucketsPerAxis 块，方形；不整除时末块按边界截断）
	const int32 RowBlock = FMath::CeilToInt((float)Cols / (float)BucketsPerAxisClamped);
	const int32 ColBlock = RowBlock;

	TArray<TArray<FTransform>> CompTransforms;
	CompTransforms.SetNum(Components.Num());

	Instances.SetNum(TotalInstances);
	for (int32 i = 0; i < TotalInstances; ++i)
	{
		const int32 RowIdx = Rand.RandRange(0, RowCaches.Num() - 1);
		const FCropRowCache& Row = RowCaches[RowIdx];
		const int32 VariantIdx = Rand.RandRange(0, Row.Variants.Num() - 1);

		// 随机初始阶段（取网格体非空的阶段，最多重抽 8 次）
		int32 Stage = -1;
		for (int32 Attempt = 0; Attempt < 8 && Stage < 0; ++Attempt)
		{
			const int32 s = Rand.RandRange(0, 3);
			if (GetVariantStageMesh(Row.Variants[VariantIdx], s))
			{
				Stage = s;
			}
		}
		if (Stage < 0)
		{
			continue; // 该变体无有效网格（数据异常），跳过
		}

		const UStaticMesh* Mesh = GetVariantStageMesh(Row.Variants[VariantIdx], Stage);
		const int32 row = i / Cols;
		const int32 col = i % Cols;
		const int32 bx = FMath::Min(col / ColBlock, BucketsPerAxisClamped - 1);
		const int32 by = FMath::Min(row / RowBlock, BucketsPerAxisClamped - 1);
		const int32 BucketIdx = by * BucketsPerAxisClamped + bx;
		const int32 CompIdx = MeshToGlobalIdx[Mesh] * NumBuckets + BucketIdx;

		const FVector Loc = OriginOffset + FVector((i % Cols) * Spacing, (i / Cols) * Spacing, 0.0f);
		CompTransforms[CompIdx].Add(FTransform(FQuat::Identity, Loc, FVector::OneVector));

		FStressInstance& Inst = Instances[i];
		Inst.CompIndex = CompIdx;
		Inst.RowIndex = RowIdx;
		Inst.VariantIndex = VariantIdx;
		Inst.Stage = Stage;
		Inst.BaseLocation = Loc;
	}

	// ④ 批量添加实例（每组件一次 AddInstancesById，返回稳定句柄），并回填到元数据
	for (int32 c = 0; c < Components.Num(); ++c)
	{
		FStressComponent& SC = Components[c];
		if (CompTransforms[c].Num() > 0)
		{
			SC.InstanceIds = SC.Comp->AddInstancesById(CompTransforms[c], /*bWorldSpace=*/true);
			// 平行数组：BaseLocations 与 InstanceIds 一一对应（构建时填充，切换时同步维护）
			SC.BaseLocations.Reserve(CompTransforms[c].Num());
			for (const FTransform& T : CompTransforms[c])
			{
				SC.BaseLocations.Add(T.GetLocation());
			}
		}
	}

	TArray<int32> CompCounter;
	CompCounter.SetNumZeroed(Components.Num());
	for (FStressInstance& Inst : Instances)
	{
		if (Inst.CompIndex == INDEX_NONE)
		{
			Inst.Id = FPrimitiveInstanceId(); // 显式置为无效句柄（防默认未初始化值误判有效）
			continue;
		}
		const int32 k = CompCounter[Inst.CompIndex]++;
		if (k < Components[Inst.CompIndex].InstanceIds.Num())
		{
			Inst.Id = Components[Inst.CompIndex].InstanceIds[k];
		}
	}

	// ⑤ 初始化 CustomData（每组件批量置 1.0）
	for (FStressComponent& SC : Components)
	{
		if (SC.InstanceIds.Num() == 0)
		{
			continue;
		}
		TArray<float> CData;
		CData.Init(1.0f, SC.InstanceIds.Num() * SC.NumCustomDataFloats);
		SC.Comp->SetCustomDataById(SC.InstanceIds, CData);
	}

	// ⑥ 构建摘要
	int32 TotalVariants = 0;
	for (const FCropRowCache& Row : RowCaches)
	{
		TotalVariants += Row.Variants.Num();
	}
	int32 NonEmptyComps = 0;
	for (const FStressComponent& SC : Components)
	{
		if (SC.InstanceIds.Num() > 0)
		{
			++NonEmptyComps;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[CropStress] ==== Begin: rows=%d variants=%d uniqueMeshes=%d buckets=%dx%d | comps=%d (nonEmpty=%d) | instances=%d (avg=%.0f/nonEmptyComp) | switches/frame=%d tfm/frame=%d cdata/frame=%d | spacing=%.0f | seed=%d ===="),
		RowCaches.Num(), TotalVariants, UniqueMeshArray.Num(), BucketsPerAxisClamped, BucketsPerAxisClamped,
		Components.Num(), NonEmptyComps,
		TotalInstances, (float)TotalInstances / (float)FMath::Max(1, NonEmptyComps),
		SwitchesPerFrame, TransformUpdatesPerFrame, CustomDataUpdatesPerFrame, Spacing, RandomSeed);

	Elapsed = 0.0;
	LastStatTime = 0.0f;
	FrameCount = 0;
	TickMsAccum = 0.0;
	SwitchMsAccum = 0.0;
	TfmMsAccum = 0.0;
	CDataMsAccum = 0.0;
	MaxTickMs = 0.0;
	TotalSwitches = 0;
	Phase = 0.0f;

	// ⑦ 相机定位（分桶对照实验：中心俯视 → 视锥/距离剔除生效）
	if (bCenterCamera)
	{
		GetWorldTimerManager().SetTimer(CameraTimerHandle, this, &AFarmlandActor::PositionCamera, 3.0f, false);
	}
}

void AFarmlandActor::PositionCamera()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || MapSize <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CropStress] PositionCamera: Pawn 不可用或地图未构建，跳过相机定位。"));
		return;
	}

	// 地图中心 = 原点（OriginOffset 已把地图居中）；相机置于中心上方俯视 X+ 方向
	const float Height = MapSize * 0.55f;
	const FRotator LookRot(-50.0f, 0.0f, 0.0f); // 俯角 50°，看向 X+
	Pawn->SetActorLocation(FVector(0.0f, 0.0f, Height));
	Pawn->SetActorRotation(LookRot);
	PC->SetControlRotation(LookRot);

	UE_LOG(LogTemp, Warning, TEXT("[CropStress] Camera positioned: map=%.0fm, camera=(0,0,%.0f) looking X+"), MapSize, Height);
}

void AFarmlandActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Components.Num() == 0 || Instances.Num() == 0)
	{
		return;
	}

	Elapsed += DeltaSeconds;
	Phase += DeltaSeconds;

	const double TickStart = FPlatformTime::Seconds();

	// ① 阶段切换：Remove 旧阶段网格实例 + Add 新阶段网格实例（与真实系统原生增量 Remove+Add 同构）
	{
		const double T0 = FPlatformTime::Seconds();
		for (int32 i = 0; i < SwitchesPerFrame; ++i)
		{
			const int32 InstIdx = FMath::RandRange(0, Instances.Num() - 1);
			FStressInstance& Inst = Instances[InstIdx];
			if (!Inst.Id.IsValid())
			{
				continue;
			}

			const FCropRowCache& Row = RowCaches[Inst.RowIndex];
			const int32 NewStage = PickRandomStage(Row, Inst.VariantIndex, Inst.Stage);
			if (NewStage < 0 || NewStage == Inst.Stage)
			{
				continue;
			}

			// 移除旧阶段实例（先取索引再移除，Remove 后句柄即失效）
			FStressComponent& OldComp = Components[Inst.CompIndex];
			const FPrimitiveInstanceId OldId = Inst.Id;
			const int32 IdxInComp = OldComp.Comp->GetInstanceIndexForId(OldId);
			OldComp.Comp->RemoveInstancesById(MakeArrayView(&OldId, 1), /*bUpdateNavigation=*/false);
			OldComp.InstanceIds.RemoveAtSwap(IdxInComp);
			OldComp.BaseLocations.RemoveAtSwap(IdxInComp);

			// 添加到新阶段实例（句柄稳定：RemoveAtSwap 只重映射不改其他实例的 Id；
			// 实例位置不变 → 区块不变，目标组件 = 新网格体 × 原区块）
			const UStaticMesh* NewMesh = GetVariantStageMesh(Row.Variants[Inst.VariantIndex], NewStage);
			const int32 BucketIdx = Inst.CompIndex % NumBuckets;
			const int32 NewCompIdx = MeshToGlobalIdx[NewMesh] * NumBuckets + BucketIdx;
			FStressComponent& NewComp = Components[NewCompIdx];
			Inst.Id = NewComp.Comp->AddInstanceById(FTransform(FQuat::Identity, Inst.BaseLocation, FVector::OneVector), /*bWorldSpace=*/true);
			Inst.CompIndex = NewCompIdx;
			Inst.Stage = NewStage;
			NewComp.InstanceIds.Add(Inst.Id);
			NewComp.BaseLocations.Add(Inst.BaseLocation);

			++TotalSwitches;
		}
		SwitchMsAccum += (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	// ② 增量变换更新：UpdateInstanceTransform 5 参版本（bMarkRenderStateDirty=false → 增量路径）
	{
		const double T0 = FPlatformTime::Seconds();
		for (int32 i = 0; i < TransformUpdatesPerFrame; ++i)
		{
			const int32 InstIdx = FMath::RandRange(0, Instances.Num() - 1);
			const FStressInstance& Inst = Instances[InstIdx];
			if (!Inst.Id.IsValid())
			{
				continue;
			}

			const float P = Phase * 1.5f + (float)(InstIdx % 997) * 0.013f;
			const FVector Loc = Inst.BaseLocation
				+ FVector(FMath::Sin(P) * 150.0f, FMath::Cos(P * 0.7f) * 150.0f, FMath::Sin(P * 1.3f) * 40.0f);
			const FTransform NewTransform(FQuat::Identity, Loc, FVector::OneVector);

			UInstancedStaticMeshComponent* Comp = Components[Inst.CompIndex].Comp;
			Comp->UpdateInstanceTransform(Comp->GetInstanceIndexForId(Inst.Id), NewTransform,
				/*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false, /*bTeleport=*/true);
		}
		TfmMsAccum += (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	// ③ CustomData 更新：SetCustomDataValueById（病害/水分呼吸）
	{
		const double T0 = FPlatformTime::Seconds();
		for (int32 i = 0; i < CustomDataUpdatesPerFrame; ++i)
		{
			const int32 InstIdx = FMath::RandRange(0, Instances.Num() - 1);
			const FStressInstance& Inst = Instances[InstIdx];
			if (!Inst.Id.IsValid())
			{
				continue;
			}

			const float Breath = 0.5f + 0.5f * FMath::Sin(Phase * 2.0f + (float)(InstIdx % 613) * 0.1f);
			Components[Inst.CompIndex].Comp->SetCustomDataValueById(Inst.Id, 0, Breath);
		}
		CDataMsAccum += (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	// ④ 统计：每 5 秒打印窗口均值 / 最大 / 帧率
	const double TickMs = (FPlatformTime::Seconds() - TickStart) * 1000.0;
	TickMsAccum += TickMs;
	MaxTickMs = FMath::Max(MaxTickMs, TickMs);
	++FrameCount;

	if (Elapsed - LastStatTime >= 5.0f)
	{
		LastStatTime = Elapsed;
		const double AvgTickMs = TickMsAccum / (double)FrameCount;
		const double AvgSwitchMs = SwitchMsAccum / (double)FrameCount;
		const double AvgTfmMs = TfmMsAccum / (double)FrameCount;
		const double AvgCDataMs = CDataMsAccum / (double)FrameCount;
		const double Fps = (double)FrameCount / 5.0;

		UE_LOG(LogTemp, Warning, TEXT("[CropStress] t=%.1fs | fps=%.1f | tick avg=%.3fms max=%.3fms | switches/frame=%d (%lld total, %.3fms) | tfm/frame=%d (%.3fms) | cdata/frame=%d (%.3fms)"),
			Elapsed, Fps, AvgTickMs, MaxTickMs,
			SwitchesPerFrame, TotalSwitches, AvgSwitchMs,
			TransformUpdatesPerFrame, AvgTfmMs,
			CustomDataUpdatesPerFrame, AvgCDataMs);

		TickMsAccum = 0.0;
		SwitchMsAccum = 0.0;
		TfmMsAccum = 0.0;
		CDataMsAccum = 0.0;
		MaxTickMs = 0.0;
		FrameCount = 0;
	}
}
