// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "SurfacePawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class APlanetActor;

UCLASS()
class PROJECTB_API ASurfacePawn : public APawn
{
	GENERATED_BODY()

public:
	ASurfacePawn();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// ==================== 增强输入系统 ====================

	// 获取输入映射上下文
	UFUNCTION(BlueprintCallable, Category = "Input")
	UInputMappingContext* GetInputMappingContext() const { return DefaultInputMappingContext; }

	// 获取移动输入动作
	UFUNCTION(BlueprintCallable, Category = "Input")
	UInputAction* GetMoveAction() const { return MoveAction; }

	// 获取旋转输入动作
	UFUNCTION(BlueprintCallable, Category = "Input")
	UInputAction* GetLookAction() const { return LookAction; }

	// 获取缩放输入动作
	UFUNCTION(BlueprintCallable, Category = "Input")
	UInputAction* GetZoomAction() const { return ZoomAction; }
	

	// ==================== 移动控制 ====================

	// 设置移动速度
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetMoveSpeed(float NewSpeed) { MoveSpeed = NewSpeed; }

	// 获取当前移动速度
	UFUNCTION(BlueprintCallable, Category = "Movement")
	float GetMoveSpeed() const { return MoveSpeed; }
	

	// 获取当前球面法线
	UFUNCTION(BlueprintCallable, Category = "Movement")
	FVector GetSurfaceNormal() const { return SurfaceNormal; }

	// 获取当前吸附的星球 Actor
	UFUNCTION(BlueprintCallable, Category = "Movement")
	APlanetActor* GetHoveredPlanet() const { return HoveredPlanet.Get(); }

	// ==================== 相机控制 ====================

	// 设置相机绕球心旋转
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void RotateCamera(float DeltaYaw, float DeltaPitch);

	// 设置相机轨道半径
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void SetCameraOrbitRadius(float NewRadius);

	// 获取当前相机轨道半径
	UFUNCTION(BlueprintCallable, Category = "Camera")
	float GetCameraOrbitRadius() const { return CameraOrbitRadius; }

protected:
	// ==================== 组件 ====================

	// 根组件（用于整体旋转）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> RootSceneComponent;

	// 弹簧臂组件（相机轨道）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> SpringArmComponent;

	// 相机组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> CameraComponent;

	// ==================== 输入资产 ====================

	// 输入映射上下文
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> DefaultInputMappingContext;

	// 移动输入动作（WSAD）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> MoveAction;

	// 旋转输入动作（鼠标）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> LookAction;

	// 缩放输入动作（滚轮）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> ZoomAction;

	// ==================== 移动参数 ====================

	// 基础移动速度（单位：cm/s）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement", meta = (DisplayName = "Move Speed", ClampMin = "0"))
	float MoveSpeed = 2000.f;

	// 相机旋转速度（单位：度/秒）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (DisplayName = "Camera Rotate Speed", ClampMin = "0"))
	float CameraRotateSpeed = 120.0f;

	// 相机缩放速度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (DisplayName = "Camera Zoom Speed", ClampMin = "0"))
	float CameraZoomSpeed = 500.0f;

	// 最小轨道半径
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (DisplayName = "Min Orbit Radius", ClampMin = "0"))
	float MinOrbitRadius = 200.0f;

	// 最大轨道半径
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (DisplayName = "Max Orbit Radius", ClampMin = "0"))
	float MaxOrbitRadius = 5000.0f;

	// 默认轨道半径
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (DisplayName = "Default Orbit Radius", ClampMin = "0"))
	float DefaultOrbitRadius = 1500.0f;

	// 当前相机轨道半径
	UPROPERTY(BlueprintReadWrite, Category = "Camera")
	float CameraOrbitRadius = 1500.0f;

	// ==================== 内部状态 ====================

	// 当前球面法线（世界空间）
	UPROPERTY(BlueprintReadWrite, Category = "Movement")
	FVector SurfaceNormal = FVector::UpVector;

	// 当前吸附的星球（弱引用：非拥有观察——星球可能先于 Pawn 销毁）
	UPROPERTY()
	TWeakObjectPtr<APlanetActor> HoveredPlanet;

	// 输入移动向量（2D，WSAD）
	FVector2D MoveInput = FVector2D::ZeroVector;

	// 输入旋转向量（2D，鼠标）
	FVector2D RotateInput = FVector2D::ZeroVector;



	// ==================== 内部方法 ====================

	// 更新球面法线
	void UpdateSurfaceNormal();

	// 处理移动输入
	void HandleMovementInput(float DeltaTime);

	// 处理相机旋转
	void HandleCameraRotation(float DeltaTime);

	// 查找最近的星球
	void FindNearestPlanet();
	
	// 处理相机缩放
	void HandleCameraZoom(float ZoomValue);
	

	// ==================== 输入回调 ====================

	// 移动输入回调
	void OnMoveInput(const FInputActionValue& Value);

	// 旋转输入回调
	void OnLookInput(const FInputActionValue& Value);

	// 缩放输入回调
	void OnZoomInput(const FInputActionValue& Value);
	
};
