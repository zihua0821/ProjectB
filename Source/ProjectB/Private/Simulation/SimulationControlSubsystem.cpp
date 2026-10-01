// SimulationControlSubsystem.cpp
#include "Simulation/SimulationControlSubsystem.h"

void USimulationControlSubsystem::SetSimulationPaused(const bool bPaused)
{
	if (bSimulationPaused == bPaused)
	{
		return;
	}
	bSimulationPaused = bPaused;
	UE_LOG(LogTemp, Log, TEXT("[SimControl] Simulation %s"), bPaused ? TEXT("PAUSED") : TEXT("RESUMED"));
}

bool USimulationControlSubsystem::IsSimulationPaused(const UWorld* World)
{
	if (World == nullptr)
	{
		return false;
	}
	if (const USimulationControlSubsystem* Subsystem = World->GetSubsystem<USimulationControlSubsystem>())
	{
		return Subsystem->IsSimulationPaused();
	}
	return false;   // 无子系统（异常/早期阶段）：视为未暂停
}
