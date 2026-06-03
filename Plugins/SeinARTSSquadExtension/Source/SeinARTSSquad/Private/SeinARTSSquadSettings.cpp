/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSSquadSettings.cpp
 * @brief   Implementation of the SeinARTS Squad Extension settings.
 */

#include "SeinARTSSquadSettings.h"

FName USeinARTSSquadSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText USeinARTSSquadSettings::GetSectionText() const
{
	return NSLOCTEXT("SeinARTSSquad", "SeinARTSSquadSettingsSection", "SeinARTS Squad Extension");
}

FText USeinARTSSquadSettings::GetSectionDescription() const
{
	return NSLOCTEXT("SeinARTSSquad", "SeinARTSSquadSettingsDescription",
		"Configure the SeinARTS Squad Extension — project-wide squad dispatch resolver default.");
}
#endif
