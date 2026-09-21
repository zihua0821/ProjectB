// OrbitManagerComponent.h
// 轨道管理组件：维护运行时轨道定义表（唯一权威结构 FOrbitDefinition）
// 注：旧"覆盖集（物理 Overlap）"逻辑已移除——覆盖判定统一为几何判定
//     （SatelliteMassFragments.h 的 IsPointInOrbitCoverage）
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OrbitManagerComponent.generated.h"

class UPlanetConfigDataAsset;

/**
 * 轨道定义（项目唯一权威——"轨道几何定义"结构）
 *
 * 四参数模型：
 * - OrbitName        轨道名称（唯一标识）
 * - Transform        轨道变换：
 *                      · Location：轨道圆心（可任意偏移，不要求过球心）
 *                      · Rotation：定义轨道平面朝向与相位零点（角度起点）
 *                          - Roll  (X)：轨道方位
 *                          - Pitch (Y)：轨道倾角
 *                          - Yaw   (Z)：相位零点（轨道起点）
 *                    （三维中确定一个平面只需两个自由度；剩余的一个自由度
 *                      即"轨道起点"——卫星角度参数化的基准方向）
 * - Radius           轨道半径（cm）
 * - ServiceDistance  服务距离（cm）——覆盖"圆柱体"的半高：
 *                      覆盖区 = 管（中线=轨道圆、半高=服务距离）∩ 球面；
 *                      配 0 时由运行时兜底"半格高"
 */
USTRUCT(BlueprintType)
struct FOrbitDefinition
{
	GENERATED_BODY()

	/** 轨道名称（唯一标识） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (DisplayName = "轨道名称"))
	FName OrbitName = NAME_None;

	/** 轨道变换（Location=圆心；Rotation=平面朝向与相位零点） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (DisplayName = "轨道变换"))
	FTransform Transform = FTransform::Identity;

	/** 轨道半径（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (DisplayName = "轨道半径(cm)"))
	float Radius = 55000.f;

	/** 服务距离（cm；覆盖圆柱半高；0 = 运行时兜底半格高） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (DisplayName = "服务距离(cm)"))
	float ServiceDistance = 0.f;
};

/**
 * 轨道管理组件
 * 维护运行时轨道定义表（初始化自 PlanetConfig->OrbitDefinitions；也可运行时增删）
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTB_API UOrbitManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOrbitManagerComponent();

	/** 设置星球配置数据 */
	void SetPlanetConfig(const TObjectPtr<UPlanetConfigDataAsset>& InPlanetConfig);

	/** 添加轨道 */
	UFUNCTION(BlueprintCallable, Category = "Orbit Management")
	void AddOrbit(FName OrbitName, const FOrbitDefinition& Definition);

	/** 移除轨道 */
	UFUNCTION(BlueprintCallable, Category = "Orbit Management")
	void RemoveOrbit(FName OrbitName);

	/** 获取轨道定义（不存在时返回默认值） */
	UFUNCTION(BlueprintPure, Category = "Orbit Management")
	FOrbitDefinition GetOrbitData(FName OrbitName) const;

	/** 检查轨道是否存在 */
	UFUNCTION(BlueprintPure, Category = "Orbit Management")
	bool HasOrbit(FName OrbitName) const;

	/** 获取所有轨道名称 */
	UFUNCTION(BlueprintPure, Category = "Orbit Management")
	TArray<FName> GetAllOrbitNames() const;

protected:
	virtual void BeginPlay() override;

private:
	/** 初始化轨道表（读取 PlanetConfig->OrbitDefinitions；空则兜底默认轨道） */
	void InitOrbit();

	UPROPERTY()
	TMap<FName, FOrbitDefinition> Orbits;

	// 星球配置
	UPROPERTY()
	TWeakObjectPtr<UPlanetConfigDataAsset> PlanetConfig;
};
