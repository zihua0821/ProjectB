// SatelliteConfigDataAsset.h
// 卫星配置数据资产（配置资产化——组件/任务/规划三处散落配置统一入口）
// 挂接：PlanetActor 的 SatelliteManagerComponent 上配置引用 → Initialize 时注入 GlobalConfigSubsystem
// 缺省策略：资产未挂接（空）→ 全走硬编码默认值（可空运行）
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SmartObjectRuntime.h"      // ESmartObjectClaimPriority（认领优先级）
#include "SatelliteConfigDataAsset.generated.h"

class UStaticMesh;
class UStateTree;

/**
 * 卫星选点评分权重（EQS 迁移版：状态权重 + 距离权重）
 * 缺口阈值化（缺水/缺肥低于阈值才算缺口），分值越高越优先
 */
USTRUCT(BlueprintType)
struct PROJECTB_API FSatelliteScoringWeights
{
	GENERATED_BODY()

	/** 成熟待收 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "成熟权重"))
	float HarvestWeight = 20.f;

	/** 已收获待重播 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "重播权重"))
	float ReplantWeight = 15.f;

	/** 杂草 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "杂草权重"))
	float WeedWeight = 30.f;

	/** 虫害 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "虫害权重"))
	float PestWeight = 30.f;

	/** 缺水（按 1-水分 连续计分） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "水分权重"))
	float MoistureWeight = 20.f;

	/** 缺肥（按 1-肥力 连续计分） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "肥力权重"))
	float FertilityWeight = 20.f;

	/** 距离权重（与卫星当前角度的角度差，度；越小越好） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "距离权重"))
	float DistanceWeight = 0.5f;

	/** 缺水判定阈值（水分低于此值才算缺口） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "水分阈值", ClampMin = "0.0", ClampMax = "1.0"))
	float MoistureThreshold = 0.7f;

	/** 缺肥判定阈值（肥力低于此值才算缺口） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "肥力阈值", ClampMin = "0.0", ClampMax = "1.0"))
	float FertilityThreshold = 0.7f;

	/** 最低选择分（最高分低于此值时不选目标） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring", meta = (DisplayName = "最低选择分"))
	float MinScoreThreshold = 1.f;
};

/**
 * 卫星配置数据资产（配置资产化——唯一配置源）
 * 内容 = 原三处散落配置统一：组件字段（卫星本体/轨道默认/转场/规划）
 *        + 评分权重 + 任务侧常量 + 规划侧常量
 */
UCLASS(BlueprintType)
class PROJECTB_API USatelliteConfigDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ==================== 卫星本体 ====================

	/** 卫星网格体（渲染用的静态网格；为空则仅创建实体不渲染） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Body", meta = (DisplayName = "卫星网格"))
	TObjectPtr<UStaticMesh> SatelliteMesh;

	/** 卫星缩放倍率（应用于网格体） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Body", meta = (DisplayName = "卫星缩放", ClampMin = "0.01"))
	float SatelliteScale = 10.0f;

	/** 卫星数量（默认轨道） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Body", meta = (ClampMin = "1", ClampMax = "10000"))
	int32 SatelliteCount = 10;

	/** 角速度（度/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Body")
	float DefaultAngularVelocity = 10.0f;

	/** 运行方向：true=顺时针, false=逆时针 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Body")
	bool bDefaultClockwise = false;

	/** 默认轨道名（为空则用 "Default"） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Body")
	FName DefaultOrbitName = NAME_None;

	/** StateTree 资产（Mass Schema；为空则只创建实体不挂 StateTree） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Body")
	TObjectPtr<UStateTree> StateTreeAsset;

	// ==================== 轨道默认 ====================

	/** 默认轨道半径（cm；未配置轨道定义时的兜底轨道） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Orbit", meta = (DisplayName = "默认轨道半径(cm)"))
	float DefaultOrbitRadius = 55000.f;

	/** 默认服务距离（覆盖带半厚 cm；未配置时为半格高） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Orbit", meta = (DisplayName = "默认服务距离(cm)"))
	float DefaultServiceDistance = 1000.f;

	// ==================== 转场 ====================

	/** 转场径向速度（cm/s；时长 T = |R_orb − R_hover| / 此值；离轨/回轨共用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Transfer", meta = (DisplayName = "转场径向速度", ClampMin = "1.0"))
	float RadialTransferSpeed = 2000.0f;

	/** 悬停高度（cm；离轨作业时悬停点距球面的固定距离） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Transfer", meta = (DisplayName = "悬停高度(cm)", ClampMin = "0.0"))
	float HoverHeight = 1000.0f;

	// ==================== 规划 ====================

	/** 轨道槽位倍数（槽数 = 卫星数 × 此值；全局决策的候选自由度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Plan", meta = (DisplayName = "轨道槽位倍数", ClampMin = "1", ClampMax = "20"))
	int32 OrbitSlotMultiplier = 5;

	/** 规划节拍间隔（秒；滚动决策频率——不等待执行完成，在途按目标槽计） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Plan", meta = (DisplayName = "规划节拍间隔", ClampMin = "0.1"))
	float OrbitPlanIntervalSeconds = 1.0f;

	/** 等待角上限（度；排座过滤——候选槽需在此角内转到悬停投影；与位移上限统一 45°） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Plan", meta = (DisplayName = "等待角上限", ClampMin = "1.0", ClampMax = "360.0"))
	float OrbitPlanWaitAngleLimitDeg = 45.0f;

	/** 整理触发线（最小相邻间距 < 均匀间距 × 此比例 才启动均匀化；0.6 = 六成） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Plan", meta = (DisplayName = "整理触发线", ClampMin = "0.1", ClampMax = "1.0"))
	float OrbitPlanRebalanceRatio = 0.6f;

	/** 均匀化独立节流（秒；每 N 秒一个整理窗口——落座阶段①仍按规划节拍滚动） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Plan", meta = (DisplayName = "均匀化节流", ClampMin = "0.1"))
	float OrbitRebalanceIntervalSeconds = 5.0f;

	/** 槽位"已过"重合容差（度；已过 ≤ 此角视为立即可用——与任务侧起飞对齐窗口一致） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Plan", meta = (DisplayName = "槽位已过容差(度)", ClampMin = "0.0"))
	float PlanSlotLeadToleranceDeg = 2.0f;

	/** 漂移位移下限（度；低于不漂——规划位移过滤） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Plan", meta = (DisplayName = "漂移位移下限(度)", ClampMin = "0.0"))
	float PlanMoveMinDeg = 3.0f;

	// ==================== 评分 ====================

	/** 选点评分权重（蓝图可配） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Scoring")
	FSatelliteScoringWeights ScoringWeights;

	// ==================== 任务 ====================

	/** 卫星认领优先级（玩家 High 可抢占） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Task", meta = (DisplayName = "认领优先级"))
	ESmartObjectClaimPriority SatelliteClaimPriority = ESmartObjectClaimPriority::Normal;

	/** 起飞时机：显示位置与提前量点的相位差容差（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Task", meta = (DisplayName = "离轨对齐容差(度)", ClampMin = "0.0"))
	float DepartAlignToleranceDeg = 1.5f;

	/** 选点节流间隔（秒；仅巡航态生效） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Task", meta = (DisplayName = "选点节流间隔(秒)", ClampMin = "0.05"))
	float SelectIntervalSeconds = 0.2f;

	/** 起飞时机：槽位接近悬停点投影的容差（度） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Task", meta = (DisplayName = "回轨对齐容差(度)", ClampMin = "0.0"))
	float TakeoffAlignToleranceDeg = 3.0f;

	/** 漂移：最短时长（秒；小位移自动放慢，不瞬移） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Task", meta = (DisplayName = "漂移最短时长(秒)", ClampMin = "0.1"))
	float DriftMinDurationSeconds = 2.0f;

	/** 漂移：最长时长（秒；后移 45° ÷ 0.75 倍角速度 = 6s） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Task", meta = (DisplayName = "漂移最长时长(秒)", ClampMin = "0.1"))
	float DriftMaxDurationSeconds = 6.0f;

	/** 作业时长缺省值（秒；认领句柄/行为定义缺失时使用） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Satellite|Task", meta = (DisplayName = "作业时长缺省(秒)", ClampMin = "0.0"))
	float DefaultActionDuration = 2.0f;
};
