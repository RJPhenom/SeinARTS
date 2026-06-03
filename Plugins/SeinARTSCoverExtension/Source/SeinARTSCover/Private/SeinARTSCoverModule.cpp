/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverModule.cpp
 */

#include "SeinARTSCoverModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSCover, Log, All);

void FSeinARTSCoverModule::StartupModule()
{
	UE_LOG(LogSeinARTSCover, Log, TEXT("SeinARTSCover module started."));
}

void FSeinARTSCoverModule::ShutdownModule()
{
	UE_LOG(LogSeinARTSCover, Log, TEXT("SeinARTSCover module shut down."));
}

IMPLEMENT_MODULE(FSeinARTSCoverModule, SeinARTSCover)
