// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProjectB : ModuleRules
{
	public ProjectB(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Niagara"});
		PublicDependencyModuleNames.AddRange(new string[] { "AIModule", "MassEntity", "MassAIBehavior", "MassSignals", "StateTreeModule", "SmartObjectsModule", "GameplayTags"  });
		// Slate UI（轨道编辑面板）：Slate/SlateCore + AppFramework（SColorPicker）+ UMG（面板 UMG 包装），均 Runtime 模块
		PublicDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "AppFramework", "UMG" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });
		

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
