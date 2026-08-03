/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentEditorGuards.cpp
 */

#include "Util/SeinSimulationContentEditorGuards.h"

#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "HAL/IConsoleManager.h"
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
}

bool FSeinSimulationContentPIEAuthorizer::RequestPIEPermission(
	bool /*bIsSimulateInEditor*/,
	FString& OutReason) const
{
	if (CVarRequireFreshManifestForPIE.GetValueOnGameThread() == 0)
	{
		OutReason.Reset();
		return true;
	}

	FSeinSimulationContentManifestBuildResult Result;
	FString Error;
	if (FSeinSimulationContentManifestBuilder::
		ValidateConfiguredManifest(Result, Error))
	{
		OutReason.Reset();
		return true;
	}

	OutReason = FString::Printf(
		TEXT("Strict SeinARTS simulation-content PIE validation failed: %s"),
		Error.IsEmpty()
			? TEXT("unknown validation error")
			: *Error);
	return false;
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
