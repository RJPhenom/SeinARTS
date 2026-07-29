/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateRecipe.h
 * @brief   Blueprint-authorable declaration and tick-zero materialization contract.
 */

#pragma once

#include "CoreMinimal.h"
#include "Data/SeinMatchSettings.h"
#include "Serialization/SeinCanonicalStateRegistry.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinCanonicalStateRecipe.generated.h"

/**
 * One passive authoritative slot declared by a canonical-state recipe.
 *
 * DefaultValue serves two purposes: its exact reflected type declares the
 * slot's root schema, and the base recipe implementation uses its value at
 * tick zero. A Blueprint recipe may derive a different initial value from the
 * immutable match settings, but it may not change the declared root type.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCanonicalStateRecipeSlotDeclaration
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State|Recipe")
	FSeinCanonicalStateValueSlotDefinition Definition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State|Recipe",
		meta = (SeinDeterministicOnly))
	FInstancedStruct DefaultValue;
};

/** One materialized tick-zero value returned by a canonical-state recipe. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCanonicalStateRecipeInitialValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State|Recipe")
	FSeinCanonicalStateKey Key;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|State|Recipe",
		meta = (SeinDeterministicOnly))
	FInstancedStruct Value;
};

/**
 * Stateless, Blueprint-authorable canonical-state recipe.
 *
 * Recipes are synchronous class-default-object strategies, not match-lifetime
 * services. Core invokes declaration before adopting restored state, and
 * invokes materialization only while constructing a new tick-zero state.
 * Implementations must be deterministic functions of MatchSettings and their
 * explicit inputs. They must not inspect UWorld, network topology, clocks,
 * random sources, mutable singletons, or process-local object identity.
 *
 * Blueprint graphs cannot retain VM stacks through this interface: both
 * operations are ordinary non-latent functions and this const CDO owns no
 * mutable per-world state. Persistent behavior and restore callbacks remain
 * in native canonical-state contributors; recipes only compose passive values.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, Const)
class SEINARTSCOREENTITY_API USeinCanonicalStateRecipe : public UObject
{
	GENERATED_BODY()

public:
	/** Globally unique identity. Freeze compares it case-insensitively as ASCII. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|State|Recipe")
	FName StableContributorID;

	/** Positive revision of this recipe's declaration contract. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|State|Recipe",
		meta = (ClampMin = "1"))
	int32 ContributorSchemaVersion = 1;

	/**
	 * Positive deterministic behavior revision. Bump when declaration or
	 * materialization semantics change without changing the schema version.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|State|Recipe",
		meta = (ClampMin = "1"))
	int32 ImplementationRevision = 1;

	/**
	 * Data-only recipe declarations. The base declaration returns this array
	 * and the base materializer returns each entry's DefaultValue.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|State|Recipe")
	TArray<FSeinCanonicalStateRecipeSlotDeclaration> DefaultSlotDeclarations;

	/**
	 * Pure declaration phase. Fresh-world restore may call this to compute the
	 * expected local schema before it considers adopting external state.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable,
		Category = "SeinARTS|State|Recipe",
		meta = (DisplayName = "Declare Canonical State Slots"))
	bool DeclareCanonicalStateSlots(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const;

	/**
	 * Tick-zero materialization phase. It must return exactly one value for
	 * every declared key and no others. It is not used to restore a checkpoint.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable,
		Category = "SeinARTS|State|Recipe",
		meta = (DisplayName = "Materialize Canonical State Values"))
	bool MaterializeCanonicalStateValues(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const;

	virtual bool DeclareCanonicalStateSlots_Implementation(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const;

	virtual bool MaterializeCanonicalStateValues_Implementation(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const;
};
