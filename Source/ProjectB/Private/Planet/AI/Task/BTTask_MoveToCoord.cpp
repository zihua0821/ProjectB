// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/AI/Task/BTTask_MoveToCoord.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "Planet/OrbitMovementComponent.h"
#include "Planet/PlanetInterface.h"
#include "StructUtils/StructView.h"

UBTTask_MoveToCoord::UBTTask_MoveToCoord()
{
	NodeName = "Move To Cell Coordinate";
	bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_MoveToCoord::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	CachedOwnerComp = &OwnerComp;
	// 1. 获取黑板
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("MoveToCoord: Blackboard is null"));
		return EBTNodeResult::Failed;
	}

	// 2. 读取目标坐标
	FInstancedStruct TargetCoordStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(TEXT("TargetCellCoord")));
	const FIntPoint TargetCoord = TargetCoordStruct.Get<FIntPoint>();

	// 3. 获取 AI Controller 和 Pawn
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("MoveToCoord: AI Controller is null"));
		return EBTNodeResult::Failed;
	}

	APawn* ControlledPawn = AIController->GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("MoveToCoord: Pawn is null"));
		return EBTNodeResult::Failed;
	}

	// 4. 获取轨道移动组件
	UOrbitMovementComponent* OrbitMovement = ControlledPawn->FindComponentByClass<UOrbitMovementComponent>();
	if (!OrbitMovement)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("MoveToCoord: OrbitMovementComponent not found on pawn"));
		return EBTNodeResult::Failed;
	}

	// 5. 添加委托
	OrbitMovement->OnMoveToTargetCompleted.AddUObject(this, &ThisClass::OnMoveCompleted);
	
	// 6. 计算目标角度
	const float TargetAngleDeg = CalculateOrbitAngleFromCell(TargetCoord, OwnerComp);

	// 7. 开始移动
	OrbitMovement->StartMoveToTargetAngle(TargetAngleDeg);
	
	return EBTNodeResult::InProgress;
}

float UBTTask_MoveToCoord::CalculateOrbitAngleFromCell(const FIntPoint& CellCoord,
	UBehaviorTreeComponent& OwnerComp)
{
    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!Blackboard)
        return 0.0f;

    // 从黑板上获取网格管理器（之前已在 AI Controller 中设置）
    UObject* PlanetActor = Blackboard->GetValueAsObject(TEXT("PlanetActor"));
    if (!PlanetActor)
    {
        UE_LOG(LogBehaviorTree, Error, TEXT("MoveToCoord: PlanetActor not found in blackboard"));
        return 0.0f;
    }

    // 获取单元格的世界位置
    FTransform CellWorldTransform = IPlanetInterface::Execute_GetCellWorldTransform(PlanetActor, CellCoord);
	
    // 获取轨道数据
	FInstancedStruct OrbitDataStruct = FInstancedStruct(Blackboard->GetValue<UBlackboardKeyType_Struct>(TEXT("OrbitData")));
	const FOrbitData& OrbitData = OrbitDataStruct.Get<FOrbitData>();
    const FVector OrbitCenter = OrbitData.Transform.GetLocation();
    const FVector OrbitNormal = OrbitData.Transform.GetRotation().GetUpVector(); 

    // 计算从轨道中心指向单元格位置的方向向量
    FVector ToCell = CellWorldTransform.GetLocation() - OrbitCenter;
    // 投影到轨道平面（去除法线方向的分量）
    FVector Projected = ToCell - FVector::DotProduct(ToCell, OrbitNormal) * OrbitNormal;
    Projected.Normalize();

    // 获取轨道平面的局部 X 轴方向（轨道零点方向）
    FVector LocalX = OrbitData.Transform.GetRotation().GetAxisX(); // X 轴为零点方向

    // 计算投影向量与 X 轴的夹角（度数）
    float Dot = FVector::DotProduct(Projected, LocalX);
    float Cross = FVector::DotProduct(OrbitNormal, FVector::CrossProduct(LocalX, Projected)); // 有符号叉积长度
    float AngleRad = FMath::Atan2(Cross, Dot);
    float AngleDeg = FMath::RadiansToDegrees(AngleRad);
    if (AngleDeg < 0.0f) AngleDeg += 360.0f;

    return AngleDeg;
}

void UBTTask_MoveToCoord::OnMoveCompleted() const
{
	if (UBehaviorTreeComponent* OwnerCompPtr = CachedOwnerComp.Get())
	{
		FinishLatentTask(*OwnerCompPtr, EBTNodeResult::Succeeded);
	}
}
