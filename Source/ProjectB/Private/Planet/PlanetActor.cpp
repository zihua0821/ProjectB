// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet/PlanetActor.h"


APlanetActor::APlanetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 创建根组件
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
	SetRootComponent(RootSceneComponent);

	// 创建球体网格组件
	SphereMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SphereMeshComponent"));
	SphereMeshComponent->SetupAttachment(RootSceneComponent);
	SphereMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// SphereMeshComponent->SetCastShadow(false); // 可选：球体本身不投射阴影

	
}

void APlanetActor::BeginPlay()
{
	Super::BeginPlay();

	
}

void APlanetActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 设置球体网格
	if (SphereMesh)
	{
		SphereMeshComponent->SetStaticMesh(SphereMesh);

		// 获取球体网格的半径
		const float SphereMeshRadius =  SphereMesh->GetBounds().SphereRadius;
		
		// 设置球体缩放
		const float Scale = Radius / SphereMeshRadius; 
		SphereMeshComponent->SetWorldScale3D(FVector(Scale));

		// 设置材质
		if (SurfaceMaterial)
		{
			SphereMeshComponent->SetMaterial(0, SurfaceMaterial);
		}
	}

	GenerateGridLines();
	// 生成地块
	// GeneratePlots();
}

void APlanetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void APlanetActor::GenerateGridLines()
{
	// 自动计算纬度步长
	LatStep = (MaxLat - MinLat) / (NumLat - 1);

	// 自动计算经度步长
	LonStep = 360.0f / NumLon;

	// 计算总地块数量
	TotalPlots = NumLat * NumLon;
	
	// 绘制纬线（平行于赤道，不同高度）
	for (int32 LatIdx = 0; LatIdx < NumLat; LatIdx++)
	{
		// 计算当前纬度（度）
		const float CurrentLat = MinLat + LatIdx * LatStep;
		// 判断是否是赤道（纬度接近 0°）
		const bool IsEquator = FMath::IsNearlyZero(CurrentLat, 0.5f); // 容差 0.5°
		// 判断是否是边界
		const bool IsBoundary = LatIdx == 0 || LatIdx == NumLat - 1;
		// 设置颜色
		FColor LatColor = FColor::Green;
		if (IsBoundary)
		{
			LatColor = FColor::Red;
		}
		else if (IsEquator)
		{
			LatColor = FColor::White;
		}
		// 设置粗细
		const float LineThickness = (IsBoundary || IsEquator) ? 5.0f : 3.0f;

		for (int32 LonIdx = 0; LonIdx < NumLon; LonIdx++)
		{
			// 环绕处理
			const int32 NextLonIdx = (LonIdx + 1) % NumLon;
			FVector P1 = LatLonToWorldCoordinates(LatIdx, LonIdx);
			FVector P2 = LatLonToWorldCoordinates(LatIdx, NextLonIdx);

			DrawDebugLine(GetWorld(), P1, P2, LatColor, true, -1.0f, 0, LineThickness);
		}
	}

	// 绘制经线（连接南北极的弧线）
	for (int32 LonIdx = 0; LonIdx < NumLon; LonIdx++)
	{
		// 设置颜色
		FColor LonColor = LonIdx == 0 ? FColor::White : FColor::Blue;
		// 设置粗细
		const float LineThickness = LonIdx == 0 ? 5.0f : 3.0f;

		for (int32 LatIdx = 0; LatIdx < NumLat - 1; LatIdx++)
		{
			FVector P1 = LatLonToWorldCoordinates(LatIdx, LonIdx);
			FVector P2 = LatLonToWorldCoordinates(LatIdx + 1, LonIdx);

			DrawDebugLine(GetWorld(), P1, P2, LonColor, true, -1.0f, 0, LineThickness);
		}
	}
}

FVector APlanetActor::LatLonToWorldCoordinates(int32 LatIdx, int32 LonIdx) const
{
	// 将索引转换为弧度
	float LatRad = FMath::DegreesToRadians(MinLat) + LatIdx * FMath::DegreesToRadians(LatStep);
	float LonRad = LonIdx * FMath::DegreesToRadians(LonStep);

	// Z 轴向上的球面坐标公式
	float CosLat = FMath::Cos(LatRad);
	float SinLat = FMath::Sin(LatRad);
	float CosLon = FMath::Cos(LonRad);
	float SinLon = FMath::Sin(LonRad);

	float X = Radius * CosLat * CosLon;
	float Y = Radius * CosLat * SinLon;
	float Z = Radius * SinLat; 

	return FVector(X, Y, Z);
}