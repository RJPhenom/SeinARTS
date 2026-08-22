/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinARTSCombatModule.cpp
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements Combat module startup and live-world teardown.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "SeinARTSCombatModule.h"

#include "Simulation/SeinWorldSubsystem.h"
#include "System/SeinCombatSubsystem.h"
#include "UObject/UObjectIterator.h"

void FSeinARTSCombatModule::StartupModule()
{
}

void FSeinARTSCombatModule::PreUnloadCallback()
{
	check(IsInGameThread());
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSCombat"),
				TEXT("combat systems and derived query state are unloading"));
		}
	}
	for (TObjectIterator<USeinCombatSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
}

void FSeinARTSCombatModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FSeinARTSCombatModule, SeinARTSCombat)
