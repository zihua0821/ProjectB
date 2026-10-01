// SimulationControlSubsystem.h
// 独立暂停系统：世界级"仿真暂停"状态（选定暂停——白名单式）
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimulationControlSubsystem.generated.h"

/**
 * 仿真控制子系统（独立暂停系统）
 * 职责：只持有"仿真是否暂停"这一个世界级状态；外部只做两件事——
 *   ① 输入状态：SetSimulationPaused(true/false)（编辑模式进/出时调用）
 *   ② 挂载白名单：参与者在 tick 入口调用统一宏 SIMULATION_PAUSE_RETURN(...)
 *
 * 当前白名单（注解备忘——运行事实以"谁调用了宏"为准，grep 宏名即列全）：
 *   · USatelliteStateTreeTickProcessor（心跳——行为总闸）
 *   · USatelliteOrbitPlanProcessor（规划）
 *   · USatelliteOrbitMovementProcessor（槽推进 + 卫星相位）
 *   · UCropGrowthProcessor（作物生长）
 * 不参与（默认运行，零改动）：格子/作物/地块视觉同步、卫星渲染同步、玩家/相机/UI、昼夜
 */
UCLASS()
class PROJECTB_API USimulationControlSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 设置仿真暂停（编辑模式进入 = true；退出 = false） */
	void SetSimulationPaused(const bool bPaused);

	/** 仿真是否暂停（实例查询） */
	bool IsSimulationPaused() const { return bSimulationPaused; }

	/** 仿真是否暂停（静态便捷：白名单参与者自检用——内部走 World 子系统查询；空世界视为未暂停） */
	static bool IsSimulationPaused(const UWorld* World);

private:
	/** 世界级仿真暂停状态（唯一运行时状态） */
	bool bSimulationPaused = false;
};

/** 白名单挂载宏（统一仪式；跨体系通用——Mass 处理器/组件/Actor 皆可一行挂载） */
#define SIMULATION_PAUSE_RETURN(World) \
	if (USimulationControlSubsystem::IsSimulationPaused(World)) { return; }
