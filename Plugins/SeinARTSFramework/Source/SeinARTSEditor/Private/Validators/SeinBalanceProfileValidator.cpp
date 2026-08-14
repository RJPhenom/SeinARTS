/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceProfileValidator.cpp
 */

#include "Validators/SeinBalanceProfileValidator.h"
#include "Balance/SeinBalanceProfile.h"
#include "Misc/DataValidation.h"
#include "Misc/PackageName.h"

#define LOCTEXT_NAMESPACE "SeinBalanceProfileValidator"

USeinBalanceProfileValidator::USeinBalanceProfileValidator()
{
	bIsEnabled = true;
}

bool USeinBalanceProfileValidator::CanValidateAsset_Implementation(const FAssetData& /*InAssetData*/, UObject* InObject, FDataValidationContext& /*InContext*/) const
{
	return Cast<USeinBalanceProfile>(InObject) != nullptr;
}

EDataValidationResult USeinBalanceProfileValidator::ValidateLoadedAsset_Implementation(const FAssetData& /*InAssetData*/, UObject* InAsset, FDataValidationContext& /*Context*/)
{
	USeinBalanceProfile* Profile = Cast<USeinBalanceProfile>(InAsset);
	if (!Profile)
	{
		return EDataValidationResult::NotValidated;
	}

	const int32 NumRoots = (Profile->TargetKind == ESeinBalanceTargetKind::Abilities)
		? Profile->AbilityRoots.Num()
		: Profile->IncludedRoots.Num();
	if (NumRoots == 0)
	{
		AssetWarning(InAsset, LOCTEXT("NoRoots",
			"Balance Data has no roots set — Gather would match nothing. Add a root class for the chosen Target Kind."));
	}
	else
	{
		// Loads the matched Blueprint classes — acceptable for an explicit save/validate of a profile.
		TArray<UClass*> Targets;
		Profile->ResolveTargetClasses(Targets);
		if (Targets.Num() == 0)
		{
			AssetWarning(InAsset, LOCTEXT("NoMatches",
				"Balance Data matches no classes — check the roots and exclusions for the chosen Target Kind."));
		}
	}

	for (int32 i = 0; i < Profile->TrackedComponents.Num(); ++i)
	{
		if (Profile->TrackedComponents[i] == nullptr)
		{
			AssetWarning(InAsset, FText::Format(
				LOCTEXT("EmptyTracked", "Tracked Components entry {0} is empty — remove it or pick a component struct."),
				FText::AsNumber(i)));
		}
	}

	const FString OutDir = Profile->OutputDir.Path;
	if (!OutDir.IsEmpty())
	{
		FText ValidationReason;
		FString ResolvedFilename;
		if (!FPackageName::IsValidLongPackageName(
			OutDir,
			false,
			&ValidationReason)
			|| !FPackageName::TryConvertLongPackageNameToFilename(
				OutDir,
				ResolvedFilename))
		{
			AssetWarning(InAsset, FText::Format(
				LOCTEXT("BadOutDir", "Output Directory '{0}' is not a valid path under a mounted content root (expected e.g. /Game/Balance) — Gather cannot create assets there. {1}"),
				FText::FromString(OutDir),
				ValidationReason.IsEmpty()
					? LOCTEXT("UnmountedOutDir", "The content root is not mounted.")
					: ValidationReason));
		}
	}

	// Config warnings never block — mark checked and pass.
	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
