// Copyright Epic Games, Inc. All Rights Reserved.

#include "SeinARTS_Framework_Editor.h"

#define LOCTEXT_NAMESPACE "FSeinARTS_Framework_EditorModule"

void FSeinARTS_Framework_EditorModule::StartupModule() {
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FSeinARTS_Framework_EditorModule::ShutdownModule() {
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSeinARTS_Framework_EditorModule, SeinARTS_Framework_Editor)