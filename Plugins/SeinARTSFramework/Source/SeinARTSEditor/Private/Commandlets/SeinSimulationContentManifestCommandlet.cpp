/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentManifestCommandlet.cpp
 */

#include "Commandlets/SeinSimulationContentManifestCommandlet.h"

#include "Util/SeinSimulationContentManifestBuilder.h"

DEFINE_LOG_CATEGORY_STATIC(
	LogSeinSimulationContentCommandlet,
	Log,
	All);

USeinSimulationContentManifestCommandlet::
	USeinSimulationContentManifestCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowProgress = false;
	UseCommandletResultAsExitCode = true;
	FastExit = false;
}

int32 USeinSimulationContentManifestCommandlet::Main(
	const FString& Params)
{
	FSeinSimulationContentManifestBuildResult Result;
	FString Error;
	if (!FSeinSimulationContentManifestBuilder::
		GenerateConfiguredManifest(Result, Error))
	{
		UE_LOG(
			LogSeinSimulationContentCommandlet,
			Error,
			TEXT("Simulation-content manifest generation failed: %s"),
			*Error);
		return 1;
	}

	UE_LOG(
		LogSeinSimulationContentCommandlet,
		Display,
		TEXT("Generated simulation-content manifest %s (%d contributors, %d records, digest=%s)."),
		*Result.ManifestObjectPath,
		Result.ContributorCount,
		Result.RecordCount,
		*Result.RootDigest.ToString(
			EGuidFormats::Digits));
	return 0;
}
