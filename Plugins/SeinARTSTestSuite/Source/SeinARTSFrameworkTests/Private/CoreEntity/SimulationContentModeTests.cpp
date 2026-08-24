/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SimulationContentModeTests.cpp
 * @author       RJ Macklem
 * @created      23 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Verifies synthesized-vs-baked simulation-content modes and coverage policy.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Serialization/SeinSimulationContentManifest.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::SeinARTSTests
{
	namespace SimulationContentMode
	{
		/** Restores the settings fields this suite mutates, including on assert exit. */
		struct FScopedContentSettings
		{
			TSoftObjectPtr<USeinSimulationContentManifest> SavedManifest;
			bool bSavedRequireCoverage = false;

			FScopedContentSettings()
			{
				const USeinARTSCoreSettings* Settings =
					GetDefault<USeinARTSCoreSettings>();
				SavedManifest = Settings->SimulationContentManifest;
				bSavedRequireCoverage =
					Settings->bRequireSimulationContentCoverage;
			}

			~FScopedContentSettings()
			{
				USeinARTSCoreSettings* Settings =
					GetMutableDefault<USeinARTSCoreSettings>();
				Settings->SimulationContentManifest = SavedManifest;
				Settings->bRequireSimulationContentCoverage =
					bSavedRequireCoverage;
			}
		};

		/** Builds a sealed manifest over the LIVE contributor registry whose only
		 *  package record deliberately does not cover the transient test world. */
		USeinSimulationContentManifest* MakeUncoveringManifest(
			FString& OutError)
		{
			FSeinSimulationContentRegistrySnapshot Snapshot;
			TArray<FSeinSimulationContentContributorRecord> Contributors;
			if (!FSeinSimulationContentRegistry::CaptureSnapshot(
					Snapshot, OutError)
				|| !FSeinSimulationContentRegistry::
					BuildManifestContributorRecords(
						Snapshot, Contributors, OutError))
			{
				return nullptr;
			}

			FSeinSimulationContentManifestProfile Profile;
			Profile.BuilderRevision = static_cast<int32>(
				FSeinSimulationContentManifestCodec::
					CurrentBuilderRevision);
			Profile.Contributors = MoveTemp(Contributors);

			TArray<uint8> SavedHash;
			SavedHash.Init(
				0x5A,
				FSeinSimulationContentManifestCodec::
					SavedPackageHashBytes);
			FSeinSimulationContentRecord& Record =
				Profile.Records.AddDefaulted_GetRef();
			Record.StableRecordKindId =
				FSeinSimulationContentManifestCodec::
					GetCurrentRecordKindId();
			Record.RecordRevision = static_cast<int32>(
				FSeinSimulationContentManifestCodec::
					CurrentRecordRevision);
			Record.CanonicalRecordId =
				TEXT("/Game/SeinFrameworkTest/NotTheTransientWorld");
			if (!FSeinSimulationContentManifestCodec::ComputeRecordDigest(
					Record.StableRecordKindId,
					FSeinSimulationContentManifestCodec::
						CurrentRecordRevision,
					Record.CanonicalRecordId,
					SavedHash,
					Record.ContentDigest,
					OutError)
				|| !FSeinSimulationContentManifestCodec::SealProfile(
					FSeinSimulationContentManifestCodec::
						CurrentFormatVersion,
					Profile,
					OutError))
			{
				return nullptr;
			}

			USeinSimulationContentManifest* Manifest =
				NewObject<USeinSimulationContentManifest>(
					GetTransientPackage(), NAME_None, RF_Transient);
			Manifest->Profiles.Add(MoveTemp(Profile));
			return Manifest;
		}
	}

	/** With no manifest configured, the world seals a records-free profile from
	 *  the live registry: content is ready, matches start, and the digest is
	 *  stable across worlds (the property peers compare at join). */
	TEST(SimulationContentSynthesizesWhenNoManifestConfigured,
		"SeinARTS.Unit.CoreEntity.SimulationContent")
	{
		using namespace SimulationContentMode;
		FScopedContentSettings Restore;
		USeinARTSCoreSettings* Settings =
			GetMutableDefault<USeinARTSCoreSettings>();
		Settings->SimulationContentManifest.Reset();
		Settings->bRequireSimulationContentCoverage = false;

		FGuid FirstDigest;
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(World));
			ASSERT_THAT(IsTrue(World->IsSimulationContentReady()));
			ASSERT_THAT(IsTrue(World->IsSimulationContentSynthesized()));
			ASSERT_THAT(IsTrue(
				World->GetSimulationContentDigest().IsValid()));
			ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
			FirstDigest = World->GetSimulationContentDigest();
		}

		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(World->IsSimulationContentSynthesized()));
		ASSERT_THAT(IsTrue(
			World->GetSimulationContentDigest() == FirstDigest));
	}

	/** With a configured manifest that does not cover the current world, coverage
	 *  is advisory by default (warn + play); refusal is the opt-in strict mode. */
	TEST(UncoveredWorldIsAdvisoryUnlessCoverageRequired,
		"SeinARTS.Unit.CoreEntity.SimulationContent")
	{
		using namespace SimulationContentMode;
		FScopedContentSettings Restore;
		FString Error;
		TStrongObjectPtr<USeinSimulationContentManifest> Manifest(
			MakeUncoveringManifest(Error));
		ASSERT_THAT(IsNotNull(Manifest.Get()));

		USeinARTSCoreSettings* Settings =
			GetMutableDefault<USeinARTSCoreSettings>();
		Settings->SimulationContentManifest = Manifest.Get();
		Settings->bRequireSimulationContentCoverage = false;

		{
			TestRunner->AddExpectedMessage(
				TEXT("outside baked Simulation Content coverage"),
				ELogVerbosity::Warning,
				EAutomationExpectedMessageFlags::Contains, 1, false);
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(World));
			ASSERT_THAT(IsTrue(World->IsSimulationContentReady()));
			ASSERT_THAT(IsFalse(
				World->IsSimulationContentSynthesized()));
			ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		}

		Settings->bRequireSimulationContentCoverage = true;
		{
			TestRunner->AddExpectedError(
				TEXT("Match bootstrap failed closed"),
				EAutomationExpectedErrorFlags::Contains, 1, false);
			TestRunner->AddExpectedError(
				TEXT("transaction closed (failed)"),
				EAutomationExpectedErrorFlags::Contains, 1, false);
			// The test-support bootstrap helper re-logs the refusal reason as
			// its own error line; expectations are claimed in registration
			// order, so the two lines above absorb the subsystem logs first.
			TestRunner->AddExpectedError(
				TEXT("absent from the selected Simulation Content profile"),
				EAutomationExpectedErrorFlags::Contains, 1, false);
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(World));
			FString StartError;
			ASSERT_THAT(IsFalse(
				SeinTestMatchBootstrap::Start(*World, &StartError)));
			ASSERT_THAT(IsTrue(StartError.Contains(TEXT(
				"absent from the selected Simulation Content profile"))));
		}
	}
}
