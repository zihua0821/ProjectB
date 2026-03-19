// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/SurfacePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Planet/PlanetActor.h"


ASurfacePawn::ASurfacePawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// 创建根组件
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	SetRootComponent(RootSceneComponent);

	// 创建弹簧臂组件（相机轨道）
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArmComponent"));
	SpringArmComponent->SetupAttachment(RootSceneComponent);
	SpringArmComponent->TargetArmLength = DefaultOrbitRadius;
	SpringArmComponent->SetRelativeRotation(FRotator(-45.0f, 0.0f, 0.0f)); // 初始俯视角度
	SpringArmComponent->bUsePawnControlRotation = false; // 不使用控制器旋转
	SpringArmComponent->bInheritPitch = false;
	SpringArmComponent->bInheritYaw = true;
	SpringArmComponent->bInheritRoll = false;
	SpringArmComponent->bDoCollisionTest = true;
	SpringArmComponent->bEnableCameraLag = true;
	SpringArmComponent->CameraLagSpeed = 10.0f;

	// 创建相机组件
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->SetupAttachment(SpringArmComponent, USpringArmComponent::SocketName);
	CameraComponent->bUsePawnControlRotation = false;

	// 设置初始相机轨道半径
	CameraOrbitRadius = DefaultOrbitRadius;
	
}

void ASurfacePawn::BeginPlay()
{
	Super::BeginPlay();

	// 添加输入映射上下文到本地玩家
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (DefaultInputMappingContext)
			{
				Subsystem->AddMappingContext(DefaultInputMappingContext, 0);
				UE_LOG(LogTemp, Log, TEXT("ASurfacePawn::BeginPlay - Added input mapping context"));
			}
		}
	}

	// 查找最近的星球
	FindNearestPlanet();
}

void ASurfacePawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 更新球面法线
	UpdateSurfaceNormal();

	// 处理移动输入
	HandleMovementInput(DeltaTime);

	// 处理相机旋转
	HandleCameraRotation(DeltaTime);
}

void ASurfacePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 转换为增强输入组件
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// 绑定移动输入（Axis2D，返回 FVector2D）
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASurfacePawn::OnMoveInput);
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Completed, this, &ASurfacePawn::OnMoveInput);

		// 绑定旋转输入（Axis2D，返回 FVector2D）
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASurfacePawn::OnLookInput);
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Completed, this, &ASurfacePawn::OnLookInput);

		// 绑定缩放输入（Axis1D，返回 float）
		EnhancedInputComponent->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ASurfacePawn::OnZoomInput);
		

		UE_LOG(LogTemp, Log, TEXT("ASurfacePawn::SetupPlayerInputComponent - Bound all input actions"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ASurfacePawn::SetupPlayerInputComponent - Failed to cast to UEnhancedInputComponent"));
	}
}

// ==================== 输入回调 ====================

void ASurfacePawn::OnMoveInput(const FInputActionValue& Value)
{
	// 获取 2D 移动向量（X=左右，Y=前后）
	MoveInput = Value.Get<FVector2D>();
}

void ASurfacePawn::OnLookInput(const FInputActionValue& Value)
{
	// 获取 2D 旋转向量（X=Yaw，Y=Pitch）
	// 注意：鼠标 Y 轴需要反转（向上拖动应该是负值）
	RotateInput = Value.Get<FVector2D>();
}

void ASurfacePawn::OnZoomInput(const FInputActionValue& Value)
{
	// 获取 1D 缩放值
	float ZoomValue = Value.Get<float>();
	HandleCameraZoom(ZoomValue);
}

// ==================== 移动控制 ====================

void ASurfacePawn::RotateCamera(float DeltaYaw, float DeltaPitch)
{
	// 旋转根组件（Yaw）
	FRotator CurrentRotation = RootSceneComponent->GetRelativeRotation();
	CurrentRotation.Yaw += DeltaYaw;
	RootSceneComponent->SetRelativeRotation(CurrentRotation);

	// 旋转弹簧臂（Pitch）
	FRotator SpringArmRotation = SpringArmComponent->GetRelativeRotation();
	SpringArmRotation.Pitch = FMath::Clamp(SpringArmRotation.Pitch + DeltaPitch, -89.0f, -10.0f);
	SpringArmComponent->SetRelativeRotation(SpringArmRotation);
}

void ASurfacePawn::SetCameraOrbitRadius(float NewRadius)
{
	CameraOrbitRadius = FMath::Clamp(NewRadius, MinOrbitRadius, MaxOrbitRadius);
	SpringArmComponent->TargetArmLength = CameraOrbitRadius;
}

// ==================== 内部方法 ====================

void ASurfacePawn::UpdateSurfaceNormal()
{
	// 查找最近的星球
	FindNearestPlanet();

	if (HoveredPlanet)
	{
		// 计算从球心到 Pawn 的向量
		FVector SphereCenter = HoveredPlanet->GetActorLocation();
		FVector ToPawn = GetActorLocation() - SphereCenter;
		
		// 归一化得到球面法线
		SurfaceNormal = ToPawn.GetSafeNormal();
	}
	else
	{
		// 默认向上
		SurfaceNormal = FVector::UpVector;
	}
}

void ASurfacePawn::HandleMovementInput(float DeltaTime)
{
	if (MoveInput.IsNearlyZero())
	{
		return;
	}

	// 获取当前 Pawn 的旋转（Yaw 方向）
	FRotator PawnRotation = RootSceneComponent->GetComponentRotation();
	
	// 计算前向和右向向量（在水平面上）
	FVector ForwardVector = PawnRotation.RotateVector(FVector::ForwardVector);
	FVector RightVector = PawnRotation.RotateVector(FVector::RightVector);

	// 将输入向量转换为世界空间方向
	FVector DesiredDirection = (ForwardVector * MoveInput.Y + RightVector * MoveInput.X).GetSafeNormal();

	// 关键：将移动方向投影到球面切平面（垂直于法线）
	FVector TangentDirection = DesiredDirection - SurfaceNormal * FVector::DotProduct(DesiredDirection, SurfaceNormal);
	
	// 如果切线方向接近零（在极点附近），使用备用方向
	if (TangentDirection.IsNearlyZero())
	{
		TangentDirection = (DesiredDirection ^ SurfaceNormal).GetSafeNormal();
	}
	
	TangentDirection.Normalize();

	// 应用移动
	FVector Movement = TangentDirection * MoveSpeed * DeltaTime;
	AddActorWorldOffset(Movement, true);

	// 高度修正：保持在球面上方固定高度
	// if (HoveredPlanet)
	// {
	// 	FVector SphereCenter = HoveredPlanet->GetActorLocation();
	// 	FVector ToPawn = GetActorLocation() - SphereCenter;
	// 	float CurrentRadius = ToPawn.Size();
	// 	
	// 	// 目标半径 = 星球半径 + 当前高度层级
	// 	float TargetRadius = HoveredPlanet->PlanetConfig->Radius;
	// 	float HeightError = CurrentRadius - TargetRadius;
	//
	// 	// 沿法线方向修正高度
	// 	if (FMath::Abs(HeightError) > 1.0f) // 容差 1cm
	// 	{
	// 		FVector HeightCorrection = -SurfaceNormal * HeightError * HeightCorrectionSpeed * DeltaTime;
	// 		AddActorWorldOffset(HeightCorrection, true);
	// 	}
	// }
}

void ASurfacePawn::HandleCameraRotation(float DeltaTime)
{
	if (RotateInput.IsNearlyZero())
	{
		return;
	}

	// 应用相机旋转
	float DeltaYaw = RotateInput.X * CameraRotateSpeed * DeltaTime;
	float DeltaPitch = RotateInput.Y * CameraRotateSpeed * DeltaTime;
	
	RotateCamera(DeltaYaw, DeltaPitch);

	// 重置旋转输入
	RotateInput = FVector2D::ZeroVector;
}

void ASurfacePawn::FindNearestPlanet()
{
	// 如果已经有吸附的星球且距离不远，继续使用
	if (HoveredPlanet)
	{
		float DistanceToCurrent = FVector::Dist(GetActorLocation(), HoveredPlanet->GetActorLocation());
		float MaxSearchRadius = HoveredPlanet->PlanetConfig->Radius * 3.0f;
		
		if (DistanceToCurrent < MaxSearchRadius)
		{
			return; // 继续使用当前星球
		}
	}

	// 查找所有星球 Actor
	TArray<AActor*> PlanetActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlanetActor::StaticClass(), PlanetActors);

	if (PlanetActors.Num() == 0)
	{
		HoveredPlanet = nullptr;
		return;
	}

	// 找到最近的星球
	AActor* NearestPlanet = nullptr;
	float MinDistance = FLT_MAX;

	for (AActor* Planet : PlanetActors)
	{
		if (!Planet)
		{
			continue;
		}

		float Distance = FVector::Dist(GetActorLocation(), Planet->GetActorLocation());
		if (Distance < MinDistance)
		{
			MinDistance = Distance;
			NearestPlanet = Planet;
		}
	}

	HoveredPlanet = Cast<APlanetActor>(NearestPlanet);

	if (HoveredPlanet)
	{
		UE_LOG(LogTemp, Log, TEXT("ASurfacePawn::FindNearestPlanet - Found planet at distance: %.2f cm"), MinDistance);
	}
}

void ASurfacePawn::HandleCameraZoom(float ZoomValue)
{
	if (FMath::Abs(ZoomValue) < KINDA_SMALL_NUMBER)
	{
		return;
	}

	// 直接缩放当前半径并钳位
	float NewOrbitRadius = CameraOrbitRadius + ZoomValue * CameraZoomSpeed;
	NewOrbitRadius = FMath::Clamp(NewOrbitRadius, MinOrbitRadius, MaxOrbitRadius);

	// 应用新半径
	SpringArmComponent->TargetArmLength = NewOrbitRadius;
	CameraOrbitRadius = NewOrbitRadius;
	
}
