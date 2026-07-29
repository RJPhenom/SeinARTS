/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateRecipe.cpp
 */

#include "Simulation/SeinCanonicalStateRecipe.h"

bool USeinCanonicalStateRecipe::DeclareCanonicalStateSlots_Implementation(
	const FSeinMatchSettings& MatchSettings,
	TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
	FString& OutError) const
{
	(void)MatchSettings;
	OutError.Reset();
	OutDeclarations = DefaultSlotDeclarations;
	return true;
}

bool USeinCanonicalStateRecipe::MaterializeCanonicalStateValues_Implementation(
	const FSeinMatchSettings& MatchSettings,
	const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
	TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
	FString& OutError) const
{
	(void)MatchSettings;
	OutError.Reset();
	OutValues.Reset(Declarations.Num());
	for (const FSeinCanonicalStateRecipeSlotDeclaration& Declaration :
		Declarations)
	{
		FSeinCanonicalStateRecipeInitialValue& Value =
			OutValues.AddDefaulted_GetRef();
		Value.Key = Declaration.Definition.Key;
		Value.Value = Declaration.DefaultValue;
	}
	return true;
}
