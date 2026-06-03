/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSNet.cpp
 */

#include "SeinARTSNet.h"

DEFINE_LOG_CATEGORY(LogSeinNet);

void FSeinARTSNetModule::StartupModule()
{
	UE_LOG(LogSeinNet, Log, TEXT("SeinARTSNet module started."));
}

void FSeinARTSNetModule::ShutdownModule()
{
	UE_LOG(LogSeinNet, Log, TEXT("SeinARTSNet module shut down."));
}

IMPLEMENT_MODULE(FSeinARTSNetModule, SeinARTSNet)
