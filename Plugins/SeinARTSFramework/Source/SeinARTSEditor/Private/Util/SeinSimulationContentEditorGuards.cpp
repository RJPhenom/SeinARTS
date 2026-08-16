/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentEditorGuards.cpp
 */

#include "Util/SeinSimulationContentEditorGuards.h"

#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Serialization/SeinSimulationContentManifest.h"
#include "Settings/PluginSettings.h"
#include "UObject/ICookInfo.h"
#include "UObject/SoftObjectPath.h"
#include "Util/SeinSimulationContentManifestBuilder.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinSimulationContentEditor, Log, All);

namespace
{
	TAutoConsoleVariable<int32> CVarRequireFreshManifestForPIE(
		TEXT("Sein.SimulationContent.RequireFreshManifestForPIE"),
		0,
		TEXT("When 1, PIE requires the saved Simulation Content Manifest to match all saved simulation-content packages. Disabled by default for normal editor iteration."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarAutoGenerateManifestForPIE(
		TEXT("Sein.SimulationContent.AutoGenerateForPIE"),
		1,
		TEXT("When 1 (default), starting PIE keeps the Simulation Content Manifest maintenance-free: an unconfigured project gets a default project-owned manifest path assigned and generated, and a stale manifest is regenerated in place. Set 0 to manage the manifest manually (CI uses Sein.SimulationContent.GenerateManifest)."),
		ECVF_Default);

	/** Canonical default asset path assigned when a project never configured
	 *  a manifest — project-owned by construction (/Game). */
	const TCHAR* DefaultManifestObjectPath =
		TEXT("/Game/SeinARTS/SimulationContentManifest.SimulationContentManifest");

	/** Best-effort freshness maintenance ahead of the strict gate. Failures
	 *  only log — the runtime bootstrap gate stays the fail-closed authority,
	 *  and strict mode (when enabled) still vetoes below. */
	void AutoMaintainConfiguredManifest()
	{
		// Never mutate project content from automation or headless runs —
		// test-profile regeneration would rewrite the tracked manifest asset
		// mid-suite. CI generates explicitly through the console command.
		if (GIsAutomationTesting || IsRunningCommandlet())
		{
			return;
		}
		USeinARTSCoreSettings* Settings =
			GetMutableDefault<USeinARTSCoreSettings>();
		if (!Settings)
		{
			return;
		}
		if (Settings->SimulationContentManifest.IsNull())
		{
			// First-time setup: assign the canonical project-owned default so
			// generation has an identity, and persist it to the project config
			// so every peer/build agrees on the path.
			Settings->SimulationContentManifest =
				TSoftObjectPtr<USeinSimulationContentManifest>(
					FSoftObjectPath(DefaultManifestObjectPath));
			Settings->TryUpdateDefaultConfigFile();
			UE_LOG(LogSeinSimulationContentEditor, Display,
				TEXT("No Simulation Content Manifest was configured; assigned the project default '%s'."),
				DefaultManifestObjectPath);
		}
		else
		{
			FSeinSimulationContentManifestBuildResult Fresh;
			FString FreshError;
			if (FSeinSimulationContentManifestBuilder::
				ValidateConfiguredManifest(Fresh, FreshError))
			{
				return; // Already exact — nothing to write.
			}
		}

		FSeinSimulationContentManifestBuildResult Result;
		FString Error;
		if (!FSeinSimulationContentManifestBuilder::
			GenerateConfiguredManifest(Result, Error))
		{
			UE_LOG(LogSeinSimulationContentEditor, Warning,
				TEXT("Automatic Simulation Content Manifest generation failed (deterministic matches will refuse to start until it succeeds): %s"),
				Error.IsEmpty()
					? TEXT("unknown generation error")
					: *Error);
			return;
		}
		UE_LOG(LogSeinSimulationContentEditor, Display,
			TEXT("Simulation Content Manifest regenerated for PIE: %s (%d contributors, %d records, digest=%s)."),
			*Result.ManifestObjectPath,
			Result.ContributorCount,
			Result.RecordCount,
			*Result.RootDigest.ToString(EGuidFormats::Digits));
	}
}

TValueOrError<bool, FText> FSeinSimulationContentPIEAuthorizer::
	IsPIEAuthorizedInternal(bool /*bIsSimulateInEditor*/) const
{
	// Maintenance first, verdict second: with auto-generation on (default) a
	// stale or missing manifest is repaired before the strict gate looks, so
	// strict mode composes instead of vetoing work the editor can do itself.
	if (CVarAutoGenerateManifestForPIE.GetValueOnGameThread() != 0)
	{
		AutoMaintainConfiguredManifest();
	}

	if (CVarRequireFreshManifestForPIE.GetValueOnGameThread() == 0)
	{
		return MakeValue(true);
	}

	FSeinSimulationContentManifestBuildResult Result;
	FString Error;
	if (FSeinSimulationContentManifestBuilder::
		ValidateConfiguredManifest(Result, Error))
	{
		return MakeValue(true);
	}

	return MakeError(FText::FromString(FString::Printf(
		TEXT("Strict SeinARTS simulation-content PIE validation failed: %s"),
		Error.IsEmpty()
			? TEXT("unknown validation error")
			: *Error)));
}

FSeinSimulationContentCookIntegration::
	FSeinSimulationContentCookIntegration()
{
	ModifyCookHandle =
		UE::Cook::FDelegates::ModifyCook.AddLambda(
			[](UE::Cook::ICookInfo& CookInfo,
				TArray<UE::Cook::FPackageCookRule>&
					InOutPackageCookRules)
			{
				FSeinSimulationContentManifestBuildResult
					Result;
				FString Error;
				if (!FSeinSimulationContentManifestBuilder::
					ValidateConfiguredManifest(
						Result,
						Error))
				{
					UE_LOG(
						LogSeinSimulationContentEditor,
						Error,
						TEXT("Cook denied by SeinARTS simulation-content preflight: %s"),
						Error.IsEmpty()
							? TEXT("unknown validation error")
							: *Error);

					// ModifyCook has no result channel. An Error log makes
					// commandlet packaging fail; explicitly queue cancellation
					// as well so an in-editor ByTheBook cook cannot continue
					// producing an unusable partial artifact.
					if (CookInfo.GetCookType()
						== UE::Cook::ECookType::ByTheBook)
					{
						static_cast<UCookOnTheFlyServer&>(
							CookInfo)
							.QueueCancelCookByTheBook();
					}
					return;
				}

				TSet<FName> PackagesToCook;
				for (const FName Package :
					Result.ContentPackages)
				{
					PackagesToCook.Add(Package);
				}
				const FName ManifestPackage(
					*FSoftObjectPath(
						Result.ManifestObjectPath)
						.GetLongPackageName());
				PackagesToCook.Add(ManifestPackage);

				const FName Instigator(
					TEXT("SeinARTSSimulationContent"));
				for (const FName Package : PackagesToCook)
				{
					if (Package.IsNone())
					{
						continue;
					}
					UE::Cook::FPackageCookRule Rule;
					Rule.PackageName = Package;
					Rule.InstigatorName = Instigator;
					Rule.CookRule =
						UE::Cook::EPackageCookRule::
							AddToCook;
					InOutPackageCookRules.Add(
						MoveTemp(Rule));
				}
			});
}

FSeinSimulationContentCookIntegration::
	~FSeinSimulationContentCookIntegration()
{
	if (ModifyCookHandle.IsValid())
	{
		UE::Cook::FDelegates::ModifyCook.Remove(
			ModifyCookHandle);
		ModifyCookHandle.Reset();
	}
}
