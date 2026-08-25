/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SimulationContentManifestBuilderTests.cpp
 * @author       RJ Macklem
 * @created      25 Aug 2026
 * @latest       25 Aug 2026
 * @brief        Qualifies project-owned manifest save-folder resolution.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "CQTest.h"

#include "Serialization/SeinSimulationContentManifest.h"
#include "Settings/PluginSettings.h"
#include "Util/SeinSimulationContentManifestBuilder.h"

namespace UE::SeinARTSTests::SimulationContentManifestBuilder
{
	struct FScopedManifestSettings
	{
		FScopedManifestSettings()
			: Settings(GetMutableDefault<USeinARTSCoreSettings>())
			, SavedFolder(Settings->ManifestSaveFolder)
			, SavedManifest(Settings->SimulationContentManifest)
		{
		}

		~FScopedManifestSettings()
		{
			Settings->ManifestSaveFolder = SavedFolder;
			Settings->SimulationContentManifest = SavedManifest;
		}

		USeinARTSCoreSettings* Settings = nullptr;
		FDirectoryPath SavedFolder;
		TSoftObjectPtr<USeinSimulationContentManifest> SavedManifest;
	};
}

TEST(ManifestSaveFolderBuildsCanonicalProjectPath,
	"SeinARTS.Editor.SimulationContent")
{
	FDirectoryPath Folder;
	Folder.Path = TEXT("/Game/ProjectEvidence/");
	FString ObjectPath;
	FString Error;
	ASSERT_THAT(IsTrue(
		FSeinSimulationContentManifestBuilder::
			BuildProjectManifestObjectPath(
				Folder,
				ObjectPath,
				Error)));
	ASSERT_THAT(AreEqual(
		FString(TEXT("/Game/ProjectEvidence/SeinSimulationContentManifest.SeinSimulationContentManifest")),
		ObjectPath));
	ASSERT_THAT(IsTrue(Error.IsEmpty()));
}

TEST(ManifestSaveFolderRejectsForeignOrMalformedMounts,
	"SeinARTS.Editor.SimulationContent")
{
	const TCHAR* InvalidFolders[] = {
		TEXT("/SeinARTSFramework/Generated"),
		TEXT("/Game//Generated"),
		TEXT("/Game/../Generated"),
		TEXT("Game/Generated"),
		TEXT("/Game\\Generated"),
	};
	for (const TCHAR* InvalidFolder : InvalidFolders)
	{
		FDirectoryPath Folder;
		Folder.Path = InvalidFolder;
		FString ObjectPath;
		FString Error;
		ASSERT_THAT(IsFalse(
			FSeinSimulationContentManifestBuilder::
				BuildProjectManifestObjectPath(
					Folder,
					ObjectPath,
					Error)));
		ASSERT_THAT(IsTrue(ObjectPath.IsEmpty()));
		ASSERT_THAT(IsFalse(Error.IsEmpty()));
	}
}

TEST(InvalidManifestSaveFolderPreservesConfiguredReference,
	"SeinARTS.Editor.SimulationContent")
{
	using namespace UE::SeinARTSTests::SimulationContentManifestBuilder;
	FScopedManifestSettings Scope;
	USeinARTSCoreSettings* Settings = Scope.Settings;
	ASSERT_THAT(IsNotNull(Settings));

	Settings->ManifestSaveFolder.Path = TEXT("/ForeignMount/Generated");
	Settings->SimulationContentManifest =
		TSoftObjectPtr<USeinSimulationContentManifest>(
			FSoftObjectPath(TEXT("/Game/Existing/Manifest.Manifest")));
	const TSoftObjectPtr<USeinSimulationContentManifest> ExpectedManifest =
		Settings->SimulationContentManifest;
	FSeinSimulationContentManifestBuildResult Result;
	FString Error;
	ASSERT_THAT(IsFalse(
		FSeinSimulationContentManifestBuilder::
			GenerateManifestInConfiguredSaveFolder(Result, Error)));
	ASSERT_THAT(IsTrue(
		Settings->SimulationContentManifest == ExpectedManifest));
	ASSERT_THAT(IsFalse(Error.IsEmpty()));
}
