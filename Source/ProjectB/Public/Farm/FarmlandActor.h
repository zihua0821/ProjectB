// FarmlandActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystem/ISMManager.h"
#include "FarmlandActor.generated.h"

UCLASS()
class PROJECTB_API AFarmlandActor : public AActor
{
	GENERATED_BODY()

public:
	AFarmlandActor();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Farmland")
	TObjectPtr<USceneComponent> RootSceneComponent;

	UPROPERTY(EditAnywhere)
	TObjectPtr<UStaticMesh> TestMesh;

	UPROPERTY(EditAnywhere)
	TObjectPtr<UStaticMesh> TestMeshB;

	// ========== 手动调用的测试入口（在蓝图中调用或C++直接调用） ==========
	UFUNCTION(BlueprintCallable, Category = "Test")
	void RunTest_Level0_SingleInstance();

	UFUNCTION(BlueprintCallable, Category = "Test")
	void RunTest_Level1_BatchPressure(int32 Count);

	UFUNCTION(BlueprintCallable, Category = "Test")
	void RunTest_Level2_CrossSwitch(int32 Count);

	UFUNCTION(BlueprintCallable, Category = "Test")
	void RunTest_Level3_FullPressure();

	UFUNCTION(BlueprintCallable, Category = "Test")
	void RunTest_Level4_SteadyState();

	UFUNCTION(BlueprintCallable, Category = "Test")
	void CleanupTest();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test")
	int32 CurrentTestLevel = -1;

	// ========== Level 4 专属配置 ==========
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test|Level4")
	TArray<TObjectPtr<UStaticMesh>> TestMeshArray;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test|Level4")
	int32 Level4TotalInstances = 10000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Test|Level4")
	int32 Level4ChangesPerFrame = 500;
private:
	// ---------- 内部实现 ----------
	void Internal_Level0_SingleInstance();
	void Internal_Level1_BatchPressure_Begin(int32 Count);
	void TestLevel1_BatchPressure_Tick(float DeltaTime);
	void Internal_Level2_CrossSwitch_Begin(int32 Count);
	void TestLevel2_CrossSwitch_Tick(float DeltaTime);
	void Internal_Level3_FullPressure();
	void Internal_Level4_SteadyState_Begin();
	void TestLevel4_SteadyState_Tick(float DeltaTime);

	// ---------- 测试状态 ----------

	int32 TestPhase = 0;
	int32 TestProgress = 0;
	int32 TestMaxCount = 0;
	int32 TestCount = 0;

	double TestStartTime = 0.0;
	double TestPhaseStartTime = 0.0;

	TArray<FGlobalInstanceId> TestIdsA;
	TArray<FGlobalInstanceId> TestIdsB;

	// ---------- Level 4 稳态测试专用 ----------
	TArray<FGlobalInstanceId> Level4InstanceIds;   // 所有活跃实例的 Id
	TArray<int32> Level4MeshIndices;                // 每个实例当前使用的 Mesh 在 TestMeshArray 中的索引
	TArray<FTransform> Level4Transforms;            // 每个实例的当前 Transform 副本

	// ---------- 第3级（原始压力）使用 ----------
	TArray<FGlobalInstanceId> GlobalInstanceIds;
	TArray<FTransform> InstanceWorldTransforms;

	static constexpr int32 TotalInstanceCount = 10000;
};