// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;
class SPlaceholderArtWindow;

class FPlaceholderArtModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// 打开窗口
	void OpenPlaceholderArtWindow();

	TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);
private:

	void RegisterMenus();


private:
	TSharedPtr<FUICommandList> PluginCommands;

	TSharedPtr<SPlaceholderArtWindow> PlaceholderArtWindow;
};
