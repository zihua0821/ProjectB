// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlaceholderArt.h"
#include "PlaceholderArtStyle.h"
#include "PlaceholderArtCommands.h"
#include "PlaceholderArtWindow.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"

static const FName PlaceholderArtTabName("PlaceholderArt");

#define LOCTEXT_NAMESPACE "FPlaceholderArtModule"

void FPlaceholderArtModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FPlaceholderArtStyle::Initialize();
	FPlaceholderArtStyle::ReloadTextures();

	FPlaceholderArtCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FPlaceholderArtCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FPlaceholderArtModule::OpenPlaceholderArtWindow),
		FCanExecuteAction());

	// 👈 注册 Tab（支持停靠）
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(PlaceholderArtTabName,
		FOnSpawnTab::CreateRaw(this, &FPlaceholderArtModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("PlaceholderArtTabTitle", "占位符工具"));
	
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPlaceholderArtModule::RegisterMenus));
}

void FPlaceholderArtModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	// 注销 Tab
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PlaceholderArtTabName);
	
	FPlaceholderArtStyle::Shutdown();

	FPlaceholderArtCommands::Unregister();
}

void FPlaceholderArtModule::OpenPlaceholderArtWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(PlaceholderArtTabName);
}

TSharedRef<SDockTab> FPlaceholderArtModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SAssignNew(PlaceholderArtWindow, SPlaceholderArtWindow)
		];
}

void FPlaceholderArtModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("Tools");
			Section.AddMenuEntryWithCommandList(FPlaceholderArtCommands::Get().PluginAction, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FPlaceholderArtCommands::Get().PluginAction));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FPlaceholderArtModule, PlaceholderArt)