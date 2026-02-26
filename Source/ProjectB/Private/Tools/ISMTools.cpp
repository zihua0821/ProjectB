// Fill out your copyright notice in the Description page of Project Settings.


#include "Tools/ISMTools.h"
#include "Components/InstancedStaticMeshComponent.h"

bool UISMTools::RemoveInstanceByTransform(UInstancedStaticMeshComponent* ISMComponent,
                                          const FTransform& TargetTransform, bool bCheckLocation, bool bCheckRotation, bool bCheckScale, float Tolerance)
{
	if (!IsValid(ISMComponent))
	{
		UE_LOG(LogTemp, Warning, TEXT("RemoveInstanceByTransform: ISM组件无效"));
		return false;
	}
	if (int32 OutIndex; GetInstanceIndexByTransform(ISMComponent, TargetTransform, OutIndex, bCheckLocation, bCheckRotation, bCheckScale, Tolerance))
	{
		ISMComponent->RemoveInstance(OutIndex);
		return true;
	}
    
	return false;
}

bool UISMTools::GetInstanceIndexByTransform(UInstancedStaticMeshComponent* ISMComponent,
	const FTransform& TargetTransform, int32& OutIndex, bool bCheckLocation, bool bCheckRotation, bool bCheckScale,
	float Tolerance)
{
	if (!IsValid(ISMComponent))
	{
		UE_LOG(LogTemp, Warning, TEXT("RemoveInstanceByTransform: ISM组件无效"));
		return false;
	}
    
	int32 InstanceCount = ISMComponent->GetInstanceCount();
	// 从后向前遍历，避免删除时索引变化
	for (int32 i = InstanceCount - 1; i >= 0; i--)
	{
		FTransform InstanceTransform;
		if (ISMComponent->GetInstanceTransform(i, InstanceTransform))
		{
			if (CompareTransforms(InstanceTransform, TargetTransform, 
								 bCheckLocation, bCheckRotation, bCheckScale, Tolerance))
			{
				OutIndex = i;
				return true;
			}
		}
	}
    
	return false;
}

bool UISMTools::AdjustTransformToGround(FTransform& InOutTransform, UWorld* World, AActor* IgnoredActor,
	bool bAlignToNormal)
{
	if (!World) return false;

	// 获取原始位置
	FVector OriginalLocation = InOutTransform.GetLocation();

	// 设置射线起点（在原始位置上方足够高处）和终点（向下足够深处）
	const float UpOffset = 1000.0f;
	const float DownOffset = 2000.0f;
	FVector TraceStart = OriginalLocation + FVector(0, 0, UpOffset);
	FVector TraceEnd = OriginalLocation - FVector(0, 0, DownOffset);

	// 设置碰撞查询参数
	FCollisionQueryParams QueryParams;
	QueryParams.bTraceComplex = true;                // 使用复杂碰撞检测
	QueryParams.AddIgnoredActor(IgnoredActor);       // 忽略自身

	// 使用对象类型查询（只检测 WorldDynamic 类型）
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FHitResult HitResult;
	if (World->LineTraceSingleByObjectType(HitResult, TraceStart, TraceEnd, ObjectQueryParams, QueryParams))
	{
		// 可选：绘制调试线
		// DrawDebugLine(World, TraceStart, HitResult.Location, FColor::Green, false, 2.0f);
		// DrawDebugPoint(World, HitResult.Location, 10.0f, FColor::Red, false, 20.0f);
		// DrawDebugSphere(World, HitResult.Location, 10.0f, 10.f, FColor::Red, false, 20.0f);

		// 调整位置 Z 值为命中点
		FVector NewLocation = OriginalLocation;
		NewLocation.Z = HitResult.Location.Z;
		InOutTransform.SetLocation(NewLocation);

		if (bAlignToNormal)
		{
			// 获取地面法线
			FVector Normal = HitResult.Normal;

			// 原旋转
			FQuat OriginalRotation = InOutTransform.GetRotation();

			// 计算从原向上向量 (0,0,1) 到法线的旋转增量
			FQuat DeltaRotation = FQuat::FindBetweenNormals(FVector::UpVector, Normal);

			// 新旋转 = 增量旋转 * 原旋转，这样物体的局部方向保持不变，但整体 Z 轴对齐法线
			FQuat NewRotation = DeltaRotation * OriginalRotation;
			InOutTransform.SetRotation(NewRotation);
		}

		return true;
	}

	// 未命中地面，返回 false，Transform 保持不变
	return false;
}

bool UISMTools::CompareTransforms(
	const FTransform& A,
	const FTransform& B,
	bool bCheckLocation,
	bool bCheckRotation,
	bool bCheckScale,
	float Tolerance)
{
	if (bCheckLocation)
	{
		float LocationDistance = FVector::Dist(A.GetLocation(), B.GetLocation());
		if (LocationDistance > Tolerance)
		{
			return false;
		}
	}
    
	if (bCheckRotation)
	{
		// 计算四元数的角度差异
		float RotationAngle = FMath::RadiansToDegrees(
			A.GetRotation().AngularDistance(B.GetRotation())
		);
		if (RotationAngle > Tolerance)
		{
			return false;
		}
	}
    
	if (bCheckScale)
	{
		FVector ScaleDiff = A.GetScale3D() - B.GetScale3D();
		float ScaleDistance = ScaleDiff.Size();
		if (ScaleDistance > Tolerance)
		{
			return false;
		}
	}
    
	return true;
}
