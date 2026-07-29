#pragma once

#include "CoreMinimal.h"
#include "Simulation/SeinCanonicalStateRecipe.h"
#include "SeinCanonicalStateRecipeTestTypes.generated.h"

class USeinWorldSubsystem;

USTRUCT(meta = (SeinDeterministic))
struct FSeinCanonicalStateRecipeTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Marker = 0;

	UPROPERTY()
	TArray<int32> OrderedValues;
};

USTRUCT(meta = (SeinDeterministic))
struct FSeinCanonicalStateRecipeAlternateTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int64 Marker = 0;
};

/** Two-slot recipe whose authored declaration and value order is non-canonical. */
UCLASS(Const)
class USeinCanonicalStateRecipeAlphaTest
	: public USeinCanonicalStateRecipe
{
	GENERATED_BODY()

public:
	USeinCanonicalStateRecipeAlphaTest();

	virtual bool DeclareCanonicalStateSlots_Implementation(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const override;

	virtual bool MaterializeCanonicalStateValues_Implementation(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const override;
};

/** One-slot recipe used to prove cross-recipe ordering. */
UCLASS(Const)
class USeinCanonicalStateRecipeBetaTest
	: public USeinCanonicalStateRecipe
{
	GENERATED_BODY()

public:
	USeinCanonicalStateRecipeBetaTest();
};

/** Different class claiming Alpha's stable contributor identity. */
UCLASS(Const)
class USeinCanonicalStateRecipeAlphaConflictTest
	: public USeinCanonicalStateRecipe
{
	GENERATED_BODY()

public:
	USeinCanonicalStateRecipeAlphaConflictTest();
};

/** Invalid declaration: two entries collapse to one canonical state key. */
UCLASS(Const)
class USeinCanonicalStateRecipeDuplicateSlotTest
	: public USeinCanonicalStateRecipe
{
	GENERATED_BODY()

public:
	USeinCanonicalStateRecipeDuplicateSlotTest();

	virtual bool DeclareCanonicalStateSlots_Implementation(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const override;
};

/** Invalid materializer: returns the wrong reflected root payload type. */
UCLASS(Const)
class USeinCanonicalStateRecipeWrongValueTypeTest
	: public USeinCanonicalStateRecipe
{
	GENERATED_BODY()

public:
	USeinCanonicalStateRecipeWrongValueTypeTest();

	virtual bool MaterializeCanonicalStateValues_Implementation(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const override;
};

/**
 * Hostile recipe used to prove that recipe calls cannot inherit the enclosing
 * gameplay materializer's world-mutation capability.
 */
UCLASS(Const)
class USeinCanonicalStateRecipeMutationProbeTest
	: public USeinCanonicalStateRecipe
{
	GENERATED_BODY()

public:
	USeinCanonicalStateRecipeMutationProbeTest();

	static void ArmMutationProbe(USeinWorldSubsystem* World);
	static void ResetMutationProbe();
	static bool WasMutationAttempted();

	virtual bool MaterializeCanonicalStateValues_Implementation(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const override;
};

/**
 * Produces transient reflected root/dynamic schema objects. Only an explicit
 * FInstancedStruct reference walk can keep them alive across a forced GC.
 */
UCLASS(Const)
class USeinCanonicalStateRecipeGCProducerTest
	: public USeinCanonicalStateRecipe
{
	GENERATED_BODY()

public:
	USeinCanonicalStateRecipeGCProducerTest();

	static void ResetGCProbe();
	static bool AreProducedSchemasAlive();

	virtual bool DeclareCanonicalStateSlots_Implementation(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const override;
};

/** Forces GC after the producer in both canonical invocation phases. */
UCLASS(Const)
class USeinCanonicalStateRecipeGCTriggerTest
	: public USeinCanonicalStateRecipe
{
	GENERATED_BODY()

public:
	USeinCanonicalStateRecipeGCTriggerTest();

	virtual bool DeclareCanonicalStateSlots_Implementation(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const override;

	virtual bool MaterializeCanonicalStateValues_Implementation(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const override;
};
