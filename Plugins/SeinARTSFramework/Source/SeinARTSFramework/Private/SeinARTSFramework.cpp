// Copyright Epic Games, Inc. All Rights Reserved.

#include "SeinARTSFramework.h"
#include "HAL/IConsoleManager.h"

#define LOCTEXT_NAMESPACE "FSeinARTSFrameworkModule"

void FSeinARTSFrameworkModule::StartupModule()
{
#if WITH_EDITOR
	// PIE disables seamless travel by default. UE forces non-seamless
	// `ServerTravel` in PIE unless `net.AllowPIESeamlessTravel=1` is set,
	// even when the GameMode opts in via `bUseSeamlessTravel = true`.
	// Non-seamless travel inherits the current GameMode class as a
	// `?game=` URL parameter (lobby's MainMenu GameMode forces itself
	// onto the gameplay map) AND tears the NetDriver down mid-travel
	// (clients get refused on reconnect, OnLogout fires, the lobby
	// nukes their bReady, and only the host actually travels). Forcing
	// the CVar on at module startup makes the editor's PIE behavior
	// match shipped — no per-project ini edits required.
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel")))
	{
		CVar->Set(1, ECVF_SetByGameOverride);
	}
#endif
}

void FSeinARTSFrameworkModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSeinARTSFrameworkModule, SeinARTSFramework)