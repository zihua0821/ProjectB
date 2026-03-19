// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/FlyingPawn.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/FloatingPawnMovement.h"


AFlyingPawn::AFlyingPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// 创建碰撞组件（球体）
	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	CollisionComponent->InitSphereRadius(35.0f);
	CollisionComponent->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	CollisionComponent->CanCharacterStepUpOn = ECB_No;
	CollisionComponent->SetShouldUpdatePhysicsVolume(true);
	CollisionComponent->SetCanEverAffectNavigation(false);
	CollisionComponent->bDynamicObstacle = true;
	RootComponent = CollisionComponent;

	// 创建移动组件（浮游移动）
	MovementComponent = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("MovementComponent"));
	MovementComponent->UpdatedComponent = CollisionComponent;

	// 创建可选的网格组件（默认使用球体，同 DefaultPawn）
	MeshComponent = CreateOptionalDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	if (MeshComponent)
	{
		MeshComponent->SetCanEverAffectNavigation(false);
		MeshComponent->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
		MeshComponent->bOwnerNoSee = true;
		MeshComponent->bCastDynamicShadow = true;

		// 加载引擎默认球体网格（可选，也可以留空让用户在蓝图中指定）
		static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/EngineMeshes/Sphere"));
		if (SphereMesh.Succeeded())
		{
			MeshComponent->SetStaticMesh(SphereMesh.Object);
		}

		// 调整大小以匹配碰撞球体半径
		float Scale = CollisionComponent->GetUnscaledSphereRadius() / 160.0f; // 默认球体半径约160
		MeshComponent->SetRelativeScale3D(FVector(Scale));
		MeshComponent->SetupAttachment(RootComponent);
		MeshComponent->SetGenerateOverlapEvents(false);
	}

	// 相机组件（直接附在根上，位于中心）
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->SetupAttachment(RootComponent);
	CameraComponent->SetRelativeLocation(FVector::ZeroVector);
}

void AFlyingPawn::BeginPlay()
{
	Super::BeginPlay();

	// 为本地玩家添加输入映射上下文
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(FlyingInputMappingContext, 0);
		}
	}
}

void AFlyingPawn::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	if (MovementComponent)
	{
		MovementComponent->MaxSpeed = MoveSpeed;
	}
}

void AFlyingPawn::OnMove(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	if (!MovementVector.IsZero())
	{
		AddMovementInput(GetActorForwardVector(), MovementVector.Y);
		AddMovementInput(GetActorRightVector(), MovementVector.X);
	}
}

void AFlyingPawn::OnLook(const FInputActionValue& Value)
{
	FVector2D LookVector = Value.Get<FVector2D>();
	if (!LookVector.IsZero())
	{
		float DeltaYaw = LookVector.X * BaseTurnRate * GetWorld()->GetDeltaSeconds();
		float DeltaPitch = LookVector.Y * BaseLookUpRate * GetWorld()->GetDeltaSeconds();

		// 使用四元数组合旋转：偏航绕世界Z轴，俯仰绕当前右向量
		FQuat CurrentQuat = GetActorQuat();
		FQuat YawQuat(GetActorUpVector(), FMath::DegreesToRadians(DeltaYaw));
		FQuat PitchQuat(GetActorRightVector(), FMath::DegreesToRadians(DeltaPitch));
		FQuat NewQuat = YawQuat * PitchQuat * CurrentQuat;

		SetActorRotation(NewQuat);
	}
}

void AFlyingPawn::OnUp(const FInputActionValue& Value)
{
	float UpValue = Value.Get<float>();
	if (UpValue != 0.0f)
	{
		AddMovementInput(GetActorUpVector(), UpValue);
	}
}

void AFlyingPawn::OnRoll(const FInputActionValue& Value)
{
	float RollValue = Value.Get<float>();
	if (RollValue != 0.0f)
	{
		float DeltaRoll = RollValue * RollRotationRate * GetWorld()->GetDeltaSeconds();
		FQuat RollQuat(GetActorForwardVector(), FMath::DegreesToRadians(DeltaRoll));
		SetActorRotation(RollQuat * GetActorQuat());
	}
}

void AFlyingPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	
}

void AFlyingPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 转换为增强输入组件
	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		// 如果不是增强输入组件（例如项目未启用增强输入插件），可以降级处理或报错
		return;
	}

	// 绑定动作
	if (MoveAction)
	{
		EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFlyingPawn::OnMove);
	}
	if (LookAction)
	{
		EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFlyingPawn::OnLook);
	}
	if (UpAction)
	{
		EnhancedInput->BindAction(UpAction, ETriggerEvent::Triggered, this, &AFlyingPawn::OnUp);
	}
	if (RollAction)
	{
		EnhancedInput->BindAction(RollAction, ETriggerEvent::Triggered, this, &AFlyingPawn::OnRoll);
	}
}

