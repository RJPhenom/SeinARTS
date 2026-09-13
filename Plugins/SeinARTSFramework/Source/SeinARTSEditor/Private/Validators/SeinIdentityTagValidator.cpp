/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinIdentityTagValidator.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Reports missing and duplicate identity tags even when generation is disabled.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Validators/SeinIdentityTagValidator.h"
#include "Util/SeinAutoTagGenerator.h"
#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "Misc/DataValidation.h"
#include "Authoring/SeinEntityComponent.h"
#include "Actor/SeinActor.h"

USeinIdentityTagValidator::USeinIdentityTagValidator()
{
	bIsEnabled = true;
}

bool USeinIdentityTagValidator::CanValidateAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&) const
{
	FGameplayTag Tag;
	bool bGenerated = false;
	const auto* BP = Cast<UBlueprint>(Object);
	return SeinAutoTag::ReadAssetIdentity(BP, Tag, bGenerated)
		|| (BP && BP->GeneratedClass && BP->GeneratedClass->IsChildOf(ASeinActor::StaticClass()));
}

EDataValidationResult USeinIdentityTagValidator::ValidateLoadedAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext&)
{
	FGameplayTag Tag;
	bool bGenerated = false;
	if (!SeinAutoTag::ReadAssetIdentity(Cast<UBlueprint>(Object), Tag, bGenerated))
	{
		const auto* BP = Cast<UBlueprint>(Object);
		TArray<const USeinIdentityComponent*> Components;
		if (BP && BP->GeneratedClass) AActor::GetActorClassDefaultComponents<USeinIdentityComponent>(BP->GeneratedClass.Get(), Components);
		if (Components.Num() > 1)
		{
			AssetFails(Object, FText::FromString(TEXT("Multiple identity components author this entity. Keep one authoritative identity component.")));
			return EDataValidationResult::Invalid;
		}
		return EDataValidationResult::NotValidated;
	}
	const FString Collision = SeinAutoTag::FindCollidingAssetPath(Tag, Object->GetOutermost()->GetName());
	if (!Tag.IsValid() || !Collision.IsEmpty())
	{
		AssetFails(Object, FText::FromString(!Tag.IsValid()
			? TEXT("Identity tag is empty. Assign a unique identity or use Initialize Tag.")
			: FString::Printf(TEXT("Identity %s is also owned by %s. Assign a distinct tag to the copied asset; do not redirect the original identity."), *Tag.ToString(), *Collision)));
		return EDataValidationResult::Invalid;
	}
	AssetPasses(Object);
	return EDataValidationResult::Valid;
}
