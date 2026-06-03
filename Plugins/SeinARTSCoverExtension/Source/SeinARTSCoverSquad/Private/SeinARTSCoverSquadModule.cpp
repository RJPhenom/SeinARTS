/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverSquadModule.cpp
 */

#include "SeinARTSCoverSquadModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSCoverSquad, Log, All);

void FSeinARTSCoverSquadModule::StartupModule()
{
	UE_LOG(LogSeinARTSCoverSquad, Log, TEXT("SeinARTSCoverSquad module started."));
}

void FSeinARTSCoverSquadModule::ShutdownModule()
{
	UE_LOG(LogSeinARTSCoverSquad, Log, TEXT("SeinARTSCoverSquad module shut down."));
}

IMPLEMENT_MODULE(FSeinARTSCoverSquadModule, SeinARTSCoverSquad)
