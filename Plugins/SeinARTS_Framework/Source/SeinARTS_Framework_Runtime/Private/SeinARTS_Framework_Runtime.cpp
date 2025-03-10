// Copyright Epic Games, Inc. All Rights Reserved.

#include "SeinARTS_Framework_Runtime.h"

#define LOCTEXT_NAMESPACE "FSeinARTS_Framework_RuntimeModule"

void FSeinARTS_Framework_RuntimeModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FSeinARTS_Framework_RuntimeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSeinARTS_Framework_RuntimeModule, SeinARTS_Framework_Runtime)