// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/Commands.h"
#include "PlaceholderArtStyle.h"

class FPlaceholderArtCommands : public TCommands<FPlaceholderArtCommands>
{
public:

	FPlaceholderArtCommands()
		: TCommands<FPlaceholderArtCommands>(TEXT("PlaceholderArt"), NSLOCTEXT("Contexts", "PlaceholderArt", "PlaceholderArt Plugin"), NAME_None, FPlaceholderArtStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
