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
