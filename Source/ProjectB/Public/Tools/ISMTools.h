// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ISMTools.generated.h"

class UInstancedStaticMeshComponent;

/**
 * ISM 操作工具类
 */
UCLASS()
class PROJECTB_API UISMTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 通过精确Transform删除实例
	 * 时间复杂度 O(N)，仅在低频操作时使用
	 * @param ISMComponent 目标ISM组件
	 * @param TargetTransform 要匹配的Transform
	 * @param bCheckLocation 是否检查位置
	 * @param bCheckRotation 是否检查旋转
	 * @param bCheckScale 是否检查缩放
	 * @param Tolerance 容差值
	 * @return 是否成功删除
	 */
	UFUNCTION(BlueprintCallable, Category = "ISM Tools|Transform")
	static bool RemoveInstanceByTransform(
		UInstancedStaticMeshComponent* ISMComponent,
		const FTransform& TargetTransform,
		bool bCheckLocation = true,
		bool bCheckRotation = false,
		bool bCheckScale = false,
		float Tolerance = 0.1f
	);

	/**
	 * 通过精确Transform获取实例Index
	 * 时间复杂度 O(N)，仅在低频操作时使用
	 * @param ISMComponent 目标ISM组件
	 * @param TargetTransform 要匹配的Transform
	 * @param OutIndex 输出的实例Index
	 * @param bCheckLocation 是否检查位置
	 * @param bCheckRotation 是否检查旋转
	 * @param bCheckScale 是否检查缩放
	 * @param Tolerance 容差值
	 * @return 是否成功获取
	 */
	UFUNCTION(BlueprintCallable, Category = "ISM Tools|Transform")
	static bool GetInstanceIndexByTransform(
		UInstancedStaticMeshComponent* ISMComponent,
		const FTransform& TargetTransform,
		int32& OutIndex,
		bool bCheckLocation = true,
		bool bCheckRotation = false,
		bool bCheckScale = false,
		float Tolerance = 0.1f
	);

	/**
	 * 调整 Transform 使其贴合地面（通过向下射线检测），可选地旋转以对齐地面法线 - 支持自定义法线
	 * @param InOutWorldTransform 输入/输出的变换（位置会被修改，旋转可能修改）
	 * @param World 世界指针
	 * @param TraceDistance 是否启用旋转对齐法线（使物体的 Z 轴对齐地面法线）
	 * @param bAlignRotation 表面法线方向（用于球面等非平面地形）
	 * @return 是否成功命中地面并调整
	*/
	UFUNCTION(BlueprintCallable, Category = "ISM Tools|Transform")
	static bool AdjustTransformToGroundWithNormal(
		FTransform& InOutWorldTransform,
		UWorld* World,
		float TraceDistance = 1000.0f,
		bool bAlignRotation = true
		);
private:
	// ================ 内部辅助函数 ================
    
	/**
	 * 比较两个Transform是否匹配
	 */
	static bool CompareTransforms(
		const FTransform& A,
		const FTransform& B,
		bool bCheckLocation,
		bool bCheckRotation,
		bool bCheckScale,
		float Tolerance
	);
};
