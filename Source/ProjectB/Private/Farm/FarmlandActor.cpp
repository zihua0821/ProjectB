// FarmlandActor.cpp
// 作物压力场景性能测试实现（ISMManager 分桶验证样例）
#include "Farm/FarmlandActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

// 构造：开启 Tick（压力循环）；Root 为纯场景挂点（ISM 组件由管理器创建并挂到 Root 下）
AFarmlandActor::AFarmlandActor()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	SetRootComponent(RootComponent);
}

// 阶段(0..3) → 阶段网格体（变体的 Seed/Seedling/Mature/Harvested）；越界/缺失返回 nullptr
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

// 随机抽一个与当前不同的阶段（仅从网格体非空的候选中抽，保证切换后 Mesh 有效）；
// 全部候选无效时返回 ExcludeStage（调用方随后因 NewStage == Stage 而跳过切换）
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

// 构建流程总览（与真实系统对齐，步骤 ①~⑥）：
// ① 解析数据表（作物行 → 行缓存） → ② 收集唯一网格体 + 每网格体 CustomData 数量 →
// ③ 随机分配实例（作物×变体×阶段）并排布 Cols×Cols 平面网格（GridCoord = 分桶输入） →
// ④ 按当前阶段网格体分组，每 Mesh 一次 BatchAddInstances（回填稳定句柄） →
// ⑤ 输出构建摘要 + DebugPrintBucketStats（V-4 桶数核对） → ⑥ 可选相机自动定位
void AFarmlandActor::BeginPlay()
{
	Super::BeginPlay();

	if (!CropDataTable)
	{
		UE_LOG(LogTemp, Error, TEXT("[CropStress] CropDataTable 未设置（请在关卡/蓝图指定作物数据表）。"));
		return;
	}

	ISMManager = GetWorld()->GetSubsystem<UISMManager>();
	if (!ISMManager)
	{
		UE_LOG(LogTemp, Error, TEXT("[CropStress] UISMManager 子系统不可用。"));
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

	// 网格体 → 每实例 CustomData 数量（同网格体跨行时取先注册行的值；存成员供 Tick 合批 Add 用）
	for (UStaticMesh* Mesh : UniqueMeshArray)
	{
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
		MeshToNumFloats.Add(Mesh, NumFloats);
	}

	// ③ 分配实例：随机作物 × 变体 × 初始阶段，平面网格排布（与真实"确定随机相位"一致，避免同步尖峰）
	Cols = FMath::CeilToInt(FMath::Sqrt((float)TotalInstances));
	const FVector OriginOffset(-Cols * Spacing * 0.5f, -Cols * Spacing * 0.5f, 0.0f);
	MapSize = Cols * Spacing;

	// 球面分桶配置注入：平面网格映射为球面网格（Lat=Lon=Cols，每格 1 实例）；
	// 关 = 不注入 = 全部单桶（分桶收益基线对照）。注意：注入为全局配置，测试地图应只放本 Actor。
	if (bConfigureSphereBuckets)
	{
		FISMSphereBucketConfig BucketCfg;
		BucketCfg.LatCells = Cols;
		BucketCfg.LonCells = Cols;
		BucketCfg.InstancesPerBucket = FMath::Max(1, InstancesPerBucket);
		ISMManager->ConfigureSphereBuckets(BucketCfg);
	}

	CachedParams.AttachParent = RootComponent;
	CachedParams.bCastShadow = true;

	// 按当前阶段网格体分组（构建时一次遍历，每网格体一次 BatchAddInstances）
	TMap<UStaticMesh*, TArray<int32>> MeshToIndices;
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

		const int32 row = i / Cols;
		const int32 col = i % Cols;
		const FVector Loc = OriginOffset + FVector(col * Spacing, row * Spacing, 0.0f);

		FStressInstance& Inst = Instances[i];
		Inst.Mesh = GetVariantStageMesh(Row.Variants[VariantIdx], Stage);
		Inst.RowIndex = RowIdx;
		Inst.VariantIndex = VariantIdx;
		Inst.Stage = Stage;
		Inst.GridCoord = FIntPoint(row, col);   // 平面格坐标 (Lat, Lon) → 球面分桶输入
		Inst.BaseLocation = Loc;
		MeshToIndices.FindOrAdd(Inst.Mesh).Add(i);
	}

	// ④ 批量添加实例（每网格体一次 BatchAddInstances，返回稳定全局句柄），并回填到元数据
	for (const auto& Pair : MeshToIndices)
	{
		UStaticMesh* Mesh = Pair.Key;
		const TArray<int32>& Indices = Pair.Value;
		const int32 NumFloats = MeshToNumFloats[Mesh];

		TArray<FTransform> Transforms;
		TArray<FIntPoint> GridCoords;
		TArray<float> CData;
		Transforms.Reserve(Indices.Num());
		GridCoords.Reserve(Indices.Num());
		CData.Reserve(Indices.Num() * NumFloats);
		for (const int32 Idx : Indices)
		{
			const FStressInstance& Inst = Instances[Idx];
			Transforms.Add(FTransform(FQuat::Identity, Inst.BaseLocation, FVector::OneVector));
			GridCoords.Add(Inst.GridCoord);
			CData.Append({1.f, 0.f, 0.f});
			// for (int32 f = 0; f < NumFloats; ++f)
			// {
			// 	CData.Add(1.0f);
			// }
		}

		const TArray<FGlobalInstanceId> Ids = ISMManager->BatchAddInstances(
			Mesh, Transforms, GridCoords, NumFloats, CachedParams,
			/*bWorldSpace=*/true, CData);
		// 返回与输入一一对应的句柄数组（保序），按实例下标回填
		for (int32 k = 0; k < Ids.Num() && k < Indices.Num(); ++k)
		{
			Instances[Indices[k]].Id = Ids[k];
		}
	}

	// ⑤ 构建摘要 + 分桶状态核对（V-4：SphereConfig / Block / BucketNum 一次输出）
	int32 TotalVariants = 0;
	for (const FCropRowCache& Row : RowCaches)
	{
		TotalVariants += Row.Variants.Num();
	}
	UE_LOG(LogTemp, Warning, TEXT("[CropStress] ==== Begin: rows=%d variants=%d uniqueMeshes=%d | sphere=%s | instances=%d (meshGroups=%d) | switches/frame=%d tfm/frame=%d cdata/frame=%d | spacing=%.0f | seed=%d ===="),
		RowCaches.Num(), TotalVariants, UniqueMeshArray.Num(),
		bConfigureSphereBuckets ? TEXT("Configured") : TEXT("Disabled"),
		TotalInstances, MeshToIndices.Num(),
		SwitchesPerFrame, TransformUpdatesPerFrame, CustomDataUpdatesPerFrame, Spacing, RandomSeed);
	ISMManager->DebugPrintBucketStats();

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

	// ⑥ 相机定位（分桶对照实验：中心俯视 → 视锥/距离剔除生效）
	if (bCenterCamera)
	{
		GetWorldTimerManager().SetTimer(CameraTimerHandle, this, &AFarmlandActor::PositionCamera, 3.0f, false);
	}

	// ⑦ 桶包围盒调试（bDebugAABB 封装：画面列表 + 自动绘制第一个 Mesh；
	// ↑/↓ 切换绘制的 Mesh，左键点击列表行亦可，Esc 关闭；
	// 规避跨 Mesh 66 层完全重叠；停用桶灰色显示 PR-6 状态）
	if (ISMManager && UniqueMeshArray.Num() > 0)
	{
		ISMManager->SetDebugAABB(true);
	}
}

// 相机定位（bCenterCamera 勾选后 3 秒触发）：地图中心正上方（高度 = MapSize×0.55）俯角 50° 看向 X+；
// 模拟"同时可见约 1/3"的局部视野——分桶对照实验的固定相机基准（同视角 A/B 对照）
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

// 每帧压力循环（与真实 Crop 系统同构的三类增量操作 + 统计）：
// ① 阶段切换：随机实例 Remove 旧阶段 + Add 新阶段（位置不变 → GridCoord 不变 → 同一空间桶）；
// ② 增量变换：BatchUpdateInstancesTransforms 按 Mesh 合批（内部按桶分组循环 ById 增量路径）；
// ③ CustomData：UpdateInstanceCustomData 单值呼吸更新（引擎原生增量）；
// ④ 统计：每 5 秒窗口输出均值/峰值/帧率（tick 分解为 switches/tfm/cdata 三段，定位 CPU 成本构成）
void AFarmlandActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!ISMManager || Instances.Num() == 0)
	{
		return;
	}

	Elapsed += DeltaSeconds;
	Phase += DeltaSeconds;

	const double TickStart = FPlatformTime::Seconds();

	// ① 阶段切换：合批（收集 Remove/Add 按 Mesh 归组 → 循环后统一批量，句柄保序回填）
	// （与真实系统 ISMManager 原生增量 Remove+Add 同构；新实例 GridCoord 不变 → 落回同一空间桶）
	{
		const double T0 = FPlatformTime::Seconds();

		// 累积缓冲：移除按“旧 Mesh”，添加按“新 Mesh”（记录实例下标供保序回填）
		TMap<UStaticMesh*, TArray<FGlobalInstanceId>> PendingRemoveByMesh;
		struct FSwitchAdd { int32 InstIdx; UStaticMesh* NewMesh; int32 NewStage; int32 NumFloats; };
		TMap<UStaticMesh*, TArray<FSwitchAdd>> PendingAddByMesh;

		// 本帧已切换的实例下标：同一实例在一帧内不会重复切换。
		// （否则同帧重复 Add 同一 Inst.Id 到 PendingRemoveByMesh，RemoveInstancesById 第二次
		//  遇到已释放 id 会使 IdToIndex 返回 INDEX_NONE -> RemoveAtSwap(-1) 断言）
		TSet<int32> SwitchedThisFrame;

		for (int32 i = 0; i < SwitchesPerFrame; ++i)
		{
			const int32 InstIdx = FMath::RandRange(0, Instances.Num() - 1);
			// 同帧已切换的实例去重，避免重复 Remove/Add 同一实例
			if (SwitchedThisFrame.Contains(InstIdx))
			{
				continue;
			}
			SwitchedThisFrame.Add(InstIdx);
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

			// 移除旧阶段（按旧 Mesh 归组）
			PendingRemoveByMesh.FindOrAdd(Inst.Mesh).Add(Inst.Id);

			// 添加到新阶段（按新 Mesh 归组）；GridCoord 不变 → 同一空间桶；CustomData 重置为 1.0
			UStaticMesh* NewMesh = GetVariantStageMesh(Row.Variants[Inst.VariantIndex], NewStage);
			const int32 NumFloats = MeshToNumFloats.Contains(NewMesh) ? MeshToNumFloats[NewMesh] : Row.NumCustomDataFloats;
			PendingAddByMesh.FindOrAdd(NewMesh).Add(FSwitchAdd{ InstIdx, NewMesh, NewStage, NumFloats });
		}

		// 批量移除（每 Mesh 一次）
		for (const auto& Pair : PendingRemoveByMesh)
			ISMManager->BatchRemoveInstances(Pair.Key, Pair.Value);

		// 批量添加（每 Mesh 一次 → 句柄保序回填到对应实例下标）
		for (const auto& Pair : PendingAddByMesh)
		{
			UStaticMesh* Mesh = Pair.Key;
			const TArray<FSwitchAdd>& AddEntries = Pair.Value;
			const int32 NumFloats = AddEntries[0].NumFloats;

			TArray<FTransform> Transforms;
			TArray<FIntPoint> GridCoords;
			TArray<float> CData;
			Transforms.Reserve(AddEntries.Num());
			GridCoords.Reserve(AddEntries.Num());
			CData.Reserve(AddEntries.Num() * NumFloats);
			for (const FSwitchAdd& Entry : AddEntries)
			{
				const FStressInstance& Inst = Instances[Entry.InstIdx];
				Transforms.Add(FTransform(FQuat::Identity, Inst.BaseLocation, FVector::OneVector));
				GridCoords.Add(Inst.GridCoord);
				for (int32 f = 0; f < NumFloats; ++f) CData.Add(1.0f);
			}

			const TArray<FGlobalInstanceId> NewIds = ISMManager->BatchAddInstances(
				Mesh, Transforms, GridCoords, NumFloats, CachedParams, /*bWorldSpace=*/true, CData);
			// 返回与输入一一对应（保序），按实例下标回填 Id/Mesh/Stage
			for (int32 k = 0; k < NewIds.Num() && k < AddEntries.Num(); ++k)
			{
				FStressInstance& Inst = Instances[AddEntries[k].InstIdx];
				Inst.Id = NewIds[k];
				Inst.Mesh = AddEntries[k].NewMesh;
				Inst.Stage = AddEntries[k].NewStage;
				++TotalSwitches;
			}
		}
		SwitchMsAccum += (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	// ② 增量变换更新：合批（按 Mesh 归组 → BatchUpdateInstancesTransforms，内部按桶分组循环 ById 增量路径）
	// （原 GetBucketComponent 直取+逐实例 UpdateInstanceTransform 收敛到管理器增量接口；bTeleport=true 物理传送）
	{
		const double T0 = FPlatformTime::Seconds();

		// 累积缓冲：Mesh → (全局句柄, 新 Transform) 对，循环后按 Mesh 一次批量更新
		struct FTfmEntry { FGlobalInstanceId Id; FTransform Tf; };
		TMap<UStaticMesh*, TArray<FTfmEntry>> PendingTfmByMesh;

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

			PendingTfmByMesh.FindOrAdd(Inst.Mesh).Add(FTfmEntry{ Inst.Id, NewTransform });
		}

		// 按 Mesh 批量更新（内部按句柄 BucketIndex 分组，组内循环引擎 UpdateInstanceTransformById）
		for (const auto& Pair : PendingTfmByMesh)
		{
			TArray<FGlobalInstanceId> Ids;
			TArray<FTransform> Tfms;
			Ids.Reserve(Pair.Value.Num());
			Tfms.Reserve(Pair.Value.Num());
			for (const FTfmEntry& Entry : Pair.Value)
			{
				Ids.Add(Entry.Id);
				Tfms.Add(Entry.Tf);
			}
			ISMManager->BatchUpdateInstancesTransforms(Pair.Key, Ids, Tfms, /*bWorldSpace=*/true, /*bTeleport=*/true);
		}
		TfmMsAccum += (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	// ③ CustomData 更新：UpdateInstanceCustomData（病害/水分呼吸，引擎原生增量路径）
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
			// ISMManager->UpdateInstanceCustomData(Inst.Mesh, Inst.Id, 0, Breath);
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
