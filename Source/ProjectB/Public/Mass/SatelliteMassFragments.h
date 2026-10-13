// SatelliteMassFragments.h
// 卫星 Mass 实体数据定义：轨道运动 / 共享数据 / 任务上下文 / 渲染句柄 fragment + 卫星标记
#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "Crops/CropData.h"          // EEditMode（目标操作类型）
#include "Mass/SatelliteManagerComponent.h"  // FOrbitSlotHandle（轨道槽认领句柄——自建槽池）
#include "SmartObjectRuntime.h"      // FSmartObjectClaimHandle（格子认领句柄——任务 fragment）
#include "Subsystem/ISMManager.h"   // FGlobalInstanceId（卫星渲染实例句柄）
#include "SatelliteMassFragments.generated.h"

class UPlanetConfigDataAsset;
class UCellManagerComponent;
class USmartObjectManagerComponent;
class USatelliteManagerComponent;

/**
 * 卫星状态：统一状态机（作业六阶段 + 漂移扩展；唯一状态字段——
 * 设计见《卫星离轨作业_需求方案.md》/《轨道全局规划_需求方案.md》）
 */
UENUM(BlueprintType)
enum class ESatelliteState : uint8
{
	Cruise        UMETA(DisplayName = "巡航"),
	Depart        UMETA(DisplayName = "待发"),
	Approach      UMETA(DisplayName = "离轨飞行"),
	Work          UMETA(DisplayName = "悬停作业"),
	ReturnWait    UMETA(DisplayName = "待回"),
	Return        UMETA(DisplayName = "回轨飞行"),
	Drift         UMETA(DisplayName = "漂移")
};

/**
 * 卫星轨道运动状态（核心 fragment——轨道相位 / 方向 / 槽位归属 / 位置覆盖）
 * 注：成员带 UPROPERTY 以便 mass.PrintEntityFragments / 调试工具显示值
 */
USTRUCT()
struct PROJECTB_API FMassSatelliteOrbitFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 所属轨道名（查 FOrbitDefinition 用） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	FName OrbitName = NAME_None;

	/** 名义轨道相位（度；按 ω 推进的时间轴状态——与物理位置/槽位分配无关；唯一相位字段）
	 *  virtual 取“名义”义（非“虚假”）；读：无槽兜底三件（渲染/离轨判据/起飞起点）；
	 *  写：初始化均分 / 到达归位（←槽相位）/ 漂移中断（←滑行相位） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	float VirtualPhase = 0.0f;

	/** 角速度（度/秒） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	float AngularVelocity = 10.0f;

	/** 运行方向：true=顺时针, false=逆时针 */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	bool bClockwise = false;

	// ---- 离轨作业：状态与位置覆盖 ----

	/** 卫星状态（唯一状态字段；各任务在 EnterState 时写入） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	ESatelliteState State = ESatelliteState::Cruise;

	/** 位置覆盖标志（离轨/悬停作业期间为 true；渲染同步优先使用覆盖位置） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	bool bOverrideLocation = false;

	/**
	 * 轨道槽位归属（自建槽池句柄——替代智能对象实例）
	 * 有效 = 持有该槽；无效 = 无槽（离轨中/未分配）
	 * 注：已认领后的槽位查询一律用本句柄（GetOrbitSlot*ByClaim），不再维护"槽索引"字段，避免双字段不同步
	 */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	FOrbitSlotHandle SlotClaimHandle;

	/** 漂移请求（规划器发单：巡航检查任务消费后转 Drift；消费即清） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	bool bDriftRequested = false;

	/** 覆盖位置（世界坐标；任务链每帧写——星球中心=世界原点的项目约定下与星球本地系等价） */
	UPROPERTY(VisibleAnywhere, Category = "Orbit")
	FVector OverrideLocation = FVector::ZeroVector;
};

/** 卫星实体标记（空 Tag，用于处理器/查询过滤） */
USTRUCT()
struct PROJECTB_API FSatelliteTag : public FMassTag
{
	GENERATED_BODY()
};

/**
 * 卫星渲染状态：ISM 实例句柄（由 SatelliteManager 注册时回写，渲染同步处理器消费）
 */
USTRUCT()
struct PROJECTB_API FSatelliteRenderFragment : public FMassFragment
{
	GENERATED_BODY()

	/** ISM 实例稳定句柄 */
	FGlobalInstanceId InstanceId;
};

/**
 * 卫星共享数据（ConstSharedFragment，按值去重：同轨道+同 Mesh 的卫星共享一份）
 * - 轨道几何：角度→世界位置的计算基础（平面圆参数化的输入变换）
 * - 渲染 Mesh：渲染同步处理器按此更新 ISM 实例
 */
USTRUCT()
struct PROJECTB_API FMassSatelliteSharedFragment : public FMassConstSharedFragment
{
	GENERATED_BODY()

	/** 轨道变换（平面方向与中心点） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	FTransform OrbitTransform = FTransform::Identity;

	/** 轨道半径（cm） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float Radius = 55000.0f;

	/** 服务距离（覆盖带半厚 cm；默认 = 半格高，轨道编辑系统可运行时调整） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float ServiceDistance = 0.0f;

	/** 卫星网格体（渲染用） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	TObjectPtr<UStaticMesh> SatelliteMesh = nullptr;

	/** 卫星缩放倍率（渲染更新时保持与注册时一致） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float SatelliteScale = 1.0f;

	/** 星球配置（球面几何反推格子坐标） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	TObjectPtr<UPlanetConfigDataAsset> PlanetConfig = nullptr;

	/** 格子管理器（目标状态查询 + 操作执行入口） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	TObjectPtr<UCellManagerComponent> CellManager = nullptr;

	/** 智能对象管理器（认领/释放） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	TObjectPtr<USmartObjectManagerComponent> SmartObjectManager = nullptr;

	/** 卫星管理组件（选点评分权重 + 转场速度） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	TObjectPtr<USatelliteManagerComponent> SatelliteManager = nullptr;
};

/**
 * 卫星任务上下文（跨 StateTree 任务传递的数据）
 * 流程状态由 StateTree 资产编排（状态不落 fragment——此处仅存任务链共享数据）
 */
USTRUCT()
struct PROJECTB_API FMassSatelliteTaskFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 目标格子坐标（无效 = 无目标） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	FIntPoint TargetCoord = FIntPoint(-1, -1);

	/**
	 * 格子认领句柄（目标格整格席位；无效 = 未认领）
	 * 语义：一次作业 = 单格 + 多任务队列；单 Slot 化——整格一次认领（不区分域）
	 * 生命周期：选点一次认领 → 保留至回轨起飞瞬间（Return::EnterState）统一释放（唯一释放点）
	 * 所有权：单句柄校验——失权 = 整单不可执行（HandleOwnershipLost）
	 */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	FSmartObjectClaimHandle CellClaimHandle;

	// ---- 离轨作业：作业队列与飞行状态 ----

	/** 作业队列（格内多操作；选点任务构建；标准 FIFO 出队，无索引） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	TArray<TEnumAsByte<EEditMode>> WorkQueue;

	/** 飞行终点（离轨=悬停点 / 回轨=入轨点；起飞时算好——路径参数一次算好） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	FVector PhaseEndLocation = FVector::ZeroVector;

	/** 飞行已进行时间（秒） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float PhaseElapsed = 0.0f;

	/** 飞行总时长（秒；路径长度 / 轨道线速度） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float PhaseDuration = 0.0f;

	// ---- 转场轨迹参数（极坐标参数方程：切向角速度 ω 同步 + 径向匀速 v_r + 垂直分量）----

	/** 起始方位角（度；轨道局部） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float PhaseThetaStartDeg = 0.0f;

	/** 切向推进量（度；回轨=+ω·T，离轨=−ω·T，含方向） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float PhaseThetaDeltaDeg = 0.0f;

	/** 起始半径（cm；平面内） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float PhaseRadiusStart = 0.0f;

	/** 终止半径（cm；平面内） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float PhaseRadiusEnd = 0.0f;

	/** 平面上方偏移起点（cm；沿轨道法线；离轨=0 / 回轨=d_hover） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float PhaseVerticalStart = 0.0f;

	/** 平面上方偏移终点（cm；沿轨道法线；离轨=d_hover / 回轨=0） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	float PhaseVerticalEnd = 0.0f;

	/** 漂移中断时刻（秒；中断→Depart 送达探针——DepartWait 首帧记录延迟并清零；0=无中断） */
	UPROPERTY(VisibleAnywhere, Category = "Satellite")
	double DriftInterruptTimeSeconds = 0.0;
};

