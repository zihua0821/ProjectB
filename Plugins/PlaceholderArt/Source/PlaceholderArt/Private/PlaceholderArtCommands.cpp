// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlaceholderArtCommands.h"

#define LOCTEXT_NAMESPACE "FPlaceholderArtModule"

void FPlaceholderArtCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "PlaceholderArt", "Execute PlaceholderArt action", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
