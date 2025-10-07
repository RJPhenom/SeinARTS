// Copyright Epic Games, Inc. All Rights Reserved.

#include "SeinARTS_Framework_Runtime.h"
#include "GameplayTagsManager.h"

#define LOCTEXT_NAMESPACE "FSeinARTS_Framework_RuntimeModule"

void FSeinARTS_Framework_RuntimeModule::StartupModule() {
	UGameplayTagsManager::Get().AddTagIniSearchPath(FPaths::ProjectPluginsDir() / TEXT("SeinARTS_Framework/Config/Tags"));
}

void FSeinARTS_Framework_RuntimeModule::ShutdownModule() {
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSeinARTS_Framework_RuntimeModule, SeinARTS_Framework_Runtime)