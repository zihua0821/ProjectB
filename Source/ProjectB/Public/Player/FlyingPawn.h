// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InputMappingContext.h"
#include "GameFramework/Pawn.h"
#include "FlyingPawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class USphereComponent;


UCLASS()
class PROJECTB_API AFlyingPawn : public APawn
{
	GENERATED_BODY()

public:
	AFlyingPawn();

protected:
	virtual void BeginPlay() override;

	virtual void PostInitializeComponents() override;
	// ==================== 组件 ====================
	
	// 碰撞组件
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionComponent;

	// 静态网格组件
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;
	
	// 移动组件
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UFloatingPawnMovement> MovementComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UCameraComponent> CameraComponent;


	// ==================== 输入资产 ====================

	// 输入映射上下文
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputMappingContext> FlyingInputMappingContext;

	// 移动输入动作（WSAD）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> MoveAction;

	// 旋转输入动作（鼠标）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInputAction> LookAction;

	// 上升/下降动作 (Ctrl Shift)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* UpAction;

	// 翻滚动作 (Q/E)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* RollAction;

	// ==================== 输入参数 ====================

	// 移动速度 (cm/s)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pawn")
	float MoveSpeed = 1200.0f;	

	// 旋转速率（度/秒）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pawn")
	float BaseTurnRate = 45.0f;

	// 仰视速率（度/秒）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pawn")
	float BaseLookUpRate = 45.0f;
	
	// 翻滚速率（度/秒）
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pawn")
	float RollRotationRate = 90.f;

	// ==================== 输入处理函数 ====================

	// 移动
	void OnMove(const FInputActionValue& Value);

	// 旋转
	void OnLook(const FInputActionValue& Value);

	// 上升/下降
	void OnUp(const FInputActionValue& Value);

	// 翻滚
	void OnRoll(const FInputActionValue& Value);
public:
	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
};
