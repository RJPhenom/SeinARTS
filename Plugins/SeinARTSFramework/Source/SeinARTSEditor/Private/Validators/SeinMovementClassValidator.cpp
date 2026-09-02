/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementClassValidator.cpp
 */

#include "Validators/SeinMovementClassValidator.h"

#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Engine/Blueprint.h"
#include "Misc/DataValidation.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "SeinMovementClassValidator"

namespace
{
	/** USeinMovement base, resolved by path so the editor module keeps no link dependency on the
	 *  Movement module (matches SeinMovementTuningExport / the movement factory). Null if Movement
	 *  isn't loaded — in which case any Movement+ class also won't resolve and is caught upstream. */
	UClass* GetSeinMovementBaseClass()
	{
		return FindObject<UClass>(nullptr, TEXT("/Script/SeinARTSMovement.SeinMovement"));
	}
}

USeinMovementClassValidator::USeinMovementClassValidator()
{
	bIsEnabled = true;
}

bool USeinMovementClassValidator::CanValidateAsset_Implementation(const FAssetData& /*InAssetData*/, UObject* InObject, FDataValidationContext& /*InContext*/) const
{
	const UBlueprint* BP = Cast<UBlueprint>(InObject);
	return BP && BP->GeneratedClass && BP->GeneratedClass->IsChildOf(ASeinActor::StaticClass());
}

EDataValidationResult USeinMovementClassValidator::ValidateLoadedAsset_Implementation(const FAssetData& /*InAssetData*/, UObject* InAsset, FDataValidationContext& /*Context*/)
{
	UBlueprint* BP = Cast<UBlueprint>(InAsset);
	if (!BP || !BP->GeneratedClass)
	{
		return EDataValidationResult::NotValidated;
	}

	ASeinActor* CDO = Cast<ASeinActor>(BP->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/ false));
	if (!CDO)
	{
		AssetPasses(InAsset);
		return EDataValidationResult::Valid;
	}

	UClass* const MovementBase = GetSeinMovementBaseClass();

	// Walk the entity bridge's authored ComponentData for FSeinMovementComponent entries and check
	// each MovementClass. (The bridge is a native subobject, so GetComponents finds it on the CDO.)
	TArray<USeinEntityBridgeComponent*> Bridges;
	CDO->GetComponents<USeinEntityBridgeComponent>(Bridges);
	for (USeinEntityBridgeComponent* Bridge : Bridges)
	{
		if (!Bridge) continue;
		for (const FInstancedStruct& Entry : Bridge->ComponentData)
		{
			if (!Entry.IsValid() || Entry.GetScriptStruct() != FSeinMovementComponent::StaticStruct()) continue;
			const FSeinMovementComponent& MoveComp = Entry.Get<FSeinMovementComponent>();

			// Empty = intentional fallback to the Basic mover; nothing to flag.
			if (MoveComp.MovementClass.IsNull()) continue;

			UClass* Resolved = Cast<UClass>(MoveComp.MovementClass.TryLoad());
			if (!Resolved)
			{
				AssetWarning(InAsset, FText::Format(
					LOCTEXT("Unresolved",
						"Movement Class '{0}' could not be loaded — units of this Blueprint will silently fall back to "
						"Basic movement. Fix the reference, or clear it to intend the Basic mover."),
					FText::FromString(MoveComp.MovementClass.ToString())));
				continue;
			}

			if (Resolved->HasAnyClassFlags(CLASS_Abstract))
			{
				AssetWarning(InAsset, FText::Format(
					LOCTEXT("Abstract",
						"Movement Class '{0}' is abstract and cannot drive a unit — it will fall back to Basic "
						"movement. Pick a concrete movement mode."),
					FText::FromString(Resolved->GetName())));
				continue;
			}

			if (MovementBase && !Resolved->IsChildOf(MovementBase))
			{
				AssetWarning(InAsset, FText::Format(
					LOCTEXT("NotMovement",
						"Movement Class '{0}' is not a Sein Movement mode — it will fall back to Basic movement."),
					FText::FromString(Resolved->GetName())));
			}
		}
	}

	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
