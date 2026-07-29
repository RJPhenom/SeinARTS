/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSUIToolkitModule.cpp
 * @brief   Module implementation for the SeinARTS UI Toolkit.
 */

#include "SeinARTSUIToolkitModule.h"
#include "Core/SeinUISubsystem.h"
#include "ViewModel/SeinLobbyViewModel.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_MODULE(FSeinARTSUIToolkit, SeinARTSUIToolkit);

void FSeinARTSUIToolkit::StartupModule()
{
}

void FSeinARTSUIToolkit::PreUnloadCallback()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSUIToolkit::ShutdownModule()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSUIToolkit::ReleaseModuleOwnedState()
{
	check(IsInGameThread());
	for (TObjectIterator<USeinUISubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
	for (TObjectIterator<USeinLobbyViewModel> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			// View models may be constructed directly from Blueprint instead
			// of through USeinUISubsystem, so they need an independent sweep.
			It->Shutdown();
			It->OnLobbyChanged.Clear();
			It->OnLocalSlotChanged.Clear();
		}
	}
}
