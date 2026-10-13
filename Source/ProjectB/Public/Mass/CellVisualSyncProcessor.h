// CellVisualSyncProcessor.h
// 格子视觉同步处理器：高亮轮廓实例的增删改（GT 外框 + 并行收集 + GT 批提交——覆盖格高亮的执行端）

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "CellVisualSyncProcessor.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTB_API UCellVisualSyncProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UCellVisualSyncProcessor();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;

	/** 轮廓计算参数快照（GT 段预提取——worker 段纯数据计算，不访问 UObject） */
	struct FCellOutlineCalcParams
	{
		float Radius = 0.f;
		float MinLat = 0.f;
		float MaxLat = 0.f;
		int32 NumLat = 0;
		int32 NumLon = 0;
		FVector OutlineExtent = FVector::ZeroVector;
		float OutlineOffsetZ = 0.f;
	};

	static TArray<FVector> CalculateCellVertexPositions(const FCellOutlineCalcParams& Params, int32 LatIdx, int32 LonIdx);

	static FTransform CalculateOutlineInstanceTransform(const FCellOutlineCalcParams& Params,
	const FTransform& CellTransform, const float LatIdx);
};
