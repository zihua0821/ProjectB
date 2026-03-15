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

bool UISMTools::AdjustTransformToGroundWithNormal(FTransform& InOutWorldTransform, UWorld* World,
	float TraceDistance, bool bAlignRotation)
{
	if (!World) return false;

	FVector Start = InOutWorldTransform.GetLocation();
	FVector Down = -InOutWorldTransform.GetUnitAxis(EAxis::Z); // 物体自身的向下方向
	// 也可以使用 HintNormal 的反方向作为向下方向，或者取重力方向（FVector::DownVector）

	FVector End = Start + Down * TraceDistance;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.bTraceComplex = true;

	// DrawDebugLine(World, Start, End, FColor::Blue, true, -1.f);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldDynamic, Params))
	{
		// DrawDebugSphere(World, Hit.Location, 10.f, 12, FColor::Green, true, -1.f);
		// DrawDebugLine(World, Start, Hit.Location, FColor::Red, true, -1.f);
		// 更新位置
		InOutWorldTransform.SetLocation(Hit.Location);

		// 如果需要调整旋转
		if (bAlignRotation)
		{
			// 获取当前旋转
			FQuat OldRot = InOutWorldTransform.GetRotation();
			// 新旋转：使 Z 轴对齐地面法线
			FQuat AlignRot = FRotationMatrix::MakeFromZX(Hit.Normal, OldRot.GetAxisX()).ToQuat();
			// 也可以保留原有的绕法线旋转（偏航），这里简单实现：
			InOutWorldTransform.SetRotation(AlignRot);
		}
		return true;
	}
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
