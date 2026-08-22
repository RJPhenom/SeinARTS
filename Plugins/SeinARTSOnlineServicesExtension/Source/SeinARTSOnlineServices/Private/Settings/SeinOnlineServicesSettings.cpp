/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesSettings.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements SOS project settings and safe provider defaults.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Settings/SeinOnlineServicesSettings.h"

#include "Provider/SeinOnlineLoopbackProvider.h"

#define LOCTEXT_NAMESPACE "SeinOnlineServicesSettings"

USeinOnlineServicesSettings::USeinOnlineServicesSettings()
{
	ProviderClass = USeinOnlineLoopbackProvider::StaticClass();
}

FName USeinOnlineServicesSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText USeinOnlineServicesSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "SeinARTS Online Services");
}

FText USeinOnlineServicesSettings::GetSectionDescription() const
{
	return LOCTEXT(
		"SectionDescription",
		"Selects the backend-neutral online-services provider and bounds its request retention.");
}
#endif

#undef LOCTEXT_NAMESPACE
