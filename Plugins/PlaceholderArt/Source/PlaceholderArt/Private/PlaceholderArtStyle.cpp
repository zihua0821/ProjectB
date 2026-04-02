// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlaceholderArtStyle.h"
#include "PlaceholderArt.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FPlaceholderArtStyle::StyleInstance = nullptr;

void FPlaceholderArtStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FPlaceholderArtStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FPlaceholderArtStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("PlaceholderArtStyle"));
	return StyleSetName;
}


const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);

TSharedRef< FSlateStyleSet > FPlaceholderArtStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("PlaceholderArtStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("PlaceholderArt")->GetBaseDir() / TEXT("Resources"));

	Style->Set("PlaceholderArt.PluginAction", new IMAGE_BRUSH_SVG(TEXT("PlaceholderButtonIcon"), Icon20x20));
	return Style;
}

void FPlaceholderArtStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FPlaceholderArtStyle::Get()
{
	return *StyleInstance;
}
