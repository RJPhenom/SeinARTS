/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentManifestBuilder.h
 * @brief   Editor-only generator and stale-evidence validator for simulation content.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Serialization/SeinSimulationContentManifest.h"
#include "Serialization/SeinSimulationContentRegistry.h"

/** Result shared by the settings UI and editor/cook admission gates. */
struct SEINARTSEDITOR_API FSeinSimulationContentManifestBuildResult
{
	FString ManifestObjectPath;
	FGuid RootDigest;
	int32 ContributorCount = 0;
	int32 RecordCount = 0;

	/** Canonical packages in the active profile, excluding the manifest itself. */
	TArray<FName> ContentPackages;
};

/**
 * Builds compatibility evidence from saved source packages.
 *
 * Authored Blueprints and data remain the source of truth. The manifest is a
 * generated artifact: generation never compiles or saves an input package, and
 * validation never mutates content.
 */
class SEINARTSEDITOR_API FSeinSimulationContentManifestBuilder
{
public:
	/** Discover and validate saved simulation inputs for cook without creating
	 *  a source manifest asset or changing project settings. */
	static bool BuildCookInputProfile(
		const FSeinSimulationContentRegistrySnapshot& Snapshot,
		FSeinSimulationContentManifestProfile& OutProfile,
		FSeinSimulationContentManifestBuildResult& OutResult,
		FString& OutError);
	/** Add actual cooked map roots and their source dependencies, excluding
	 *  engine/platform-generated presentation packages. Does not load assets. */
	static bool CollectCookSourcePackages(
		const FSeinSimulationContentRegistrySnapshot& Snapshot,
		TConstArrayView<FName> InputPackages,
		TConstArrayView<FName> CookedPackages,
		TArray<FName>& OutPackages, FString& OutError);
	/** Validate compiled source contracts after cook without interpreting cooker dirtiness as author edits. */
	static bool ValidateCookSourceContracts(TConstArrayView<FName> Packages, FString& OutError);
	/** Resolve a canonical project-owned manifest object path from a save folder. */
	static bool BuildProjectManifestObjectPath(
		const FDirectoryPath& SaveFolder,
		FString& OutObjectPath,
		FString& OutError);

	/**
	 * Generate at ManifestSaveFolder and persist SimulationContentManifest only
	 * after the asset saves successfully. The previous reference survives any
	 * validation or generation failure.
	 */
	static bool GenerateManifestInConfiguredSaveFolder(
		FSeinSimulationContentManifestBuildResult& OutResult,
		FString& OutError);

	/**
	 * Regenerate the exact active contributor-set profile and save the
	 * configured USeinSimulationContentManifest asset. Other valid profiles
	 * are preserved.
	 */
	static bool GenerateConfiguredManifest(
		FSeinSimulationContentManifestBuildResult& OutResult,
		FString& OutError);

	/**
	 * Rebuild the expected profile without writing and require the configured
	 * manifest to match it exactly.
	 */
	static bool ValidateConfiguredManifest(
		FSeinSimulationContentManifestBuildResult& OutResult,
		FString& OutError);
};
