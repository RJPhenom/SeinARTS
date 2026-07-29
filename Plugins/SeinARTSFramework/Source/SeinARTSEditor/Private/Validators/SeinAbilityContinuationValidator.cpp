#include "Validators/SeinAbilityContinuationValidator.h"

#include "Engine/Blueprint.h"
#include "Misc/DataValidation.h"
#include "Validators/SeinAbilityContinuationAnalysis.h"

USeinAbilityContinuationValidator::USeinAbilityContinuationValidator()
{
	bIsEnabled = true;
}

bool USeinAbilityContinuationValidator::CanValidateAsset_Implementation(
	const FAssetData&,
	UObject* InObject,
	FDataValidationContext&) const
{
	return FSeinAbilityContinuationAnalysis::IsAbilityBlueprint(
		Cast<UBlueprint>(InObject));
}

EDataValidationResult
USeinAbilityContinuationValidator::ValidateLoadedAsset_Implementation(
	const FAssetData&,
	UObject* InAsset,
	FDataValidationContext&)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(InAsset);
	if (!FSeinAbilityContinuationAnalysis::IsAbilityBlueprint(Blueprint))
	{
		return EDataValidationResult::NotValidated;
	}

	TArray<FSeinAbilityContinuationFinding> Findings;
	FSeinAbilityContinuationAnalysis::Analyze(*Blueprint, Findings);
	if (Findings.IsEmpty())
	{
		AssetPasses(InAsset);
		return EDataValidationResult::Valid;
	}

	for (const FSeinAbilityContinuationFinding& Finding : Findings)
	{
		AssetFails(InAsset, FText::FromString(Finding.ToDiagnostic()));
	}
	return EDataValidationResult::Invalid;
}
