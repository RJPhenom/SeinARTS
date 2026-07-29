/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateRecipeRegistry.h
 * @brief   Extension-safe registry for canonical-state recipe class paths.
 */

#pragma once

#include "CoreMinimal.h"
#include "Simulation/SeinCanonicalStateRecipe.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"

/** Frozen semantic identity of one registered recipe class. */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateRecipeDescriptor
{
	FName StableContributorID;
	uint32 ContributorSchemaVersion = 0;
	uint32 ImplementationRevision = 0;
	FString RecipeClassPath;
};

/**
 * Canonical declaration output for one recipe.
 *
 * CanonicalSlotDescriptors aligns with Slots. Flatten these arrays across the
 * frozen recipes and pass them to
 * FSeinCanonicalStateRegistry::BuildCombinedContractIdentity. This lets a
 * fresh restore compute its expected local schema before adopting snapshot
 * state.
 */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateRecipeDeclaration
{
	FSeinCanonicalStateRecipeDescriptor Recipe;
	TArray<FSeinCanonicalStateRecipeSlotDeclaration> Slots;
	TArray<FString> CanonicalSlotDescriptors;
};

/** Validated, canonically ordered tick-zero values from one recipe. */
struct SEINARTSCOREENTITY_API FSeinCanonicalStateRecipeMaterialization
{
	FSeinCanonicalStateRecipeDescriptor Recipe;
	TArray<FSeinCanonicalStateRecipeInitialValue> Values;
};

/**
 * Move-only module-generation lease. Successful claims register a recipe;
 * failed claims retain a poison lease so Freeze cannot silently omit that
 * live module generation. Reset/destruction removes only this exact lease.
 */
class SEINARTSCOREENTITY_API
	FSeinCanonicalStateRecipeRegistrationHandle
{
public:
	FSeinCanonicalStateRecipeRegistrationHandle() = default;
	~FSeinCanonicalStateRecipeRegistrationHandle();

	FSeinCanonicalStateRecipeRegistrationHandle(
		const FSeinCanonicalStateRecipeRegistrationHandle&) = delete;
	FSeinCanonicalStateRecipeRegistrationHandle& operator=(
		const FSeinCanonicalStateRecipeRegistrationHandle&) = delete;

	FSeinCanonicalStateRecipeRegistrationHandle(
		FSeinCanonicalStateRecipeRegistrationHandle&& Other) noexcept;
	FSeinCanonicalStateRecipeRegistrationHandle& operator=(
		FSeinCanonicalStateRecipeRegistrationHandle&& Other) noexcept;

	/**
	 * True only for a successful recipe claim. A failed claim may still retain
	 * an internal lease until Reset/destruction so Freeze remains fail-closed.
	 */
	bool IsValid() const
	{
		return Token != 0 && bRegistrationSucceeded;
	}
	void Reset();

private:
	explicit FSeinCanonicalStateRecipeRegistrationHandle(
		uint64 InToken,
		bool bInRegistrationSucceeded)
		: Token(InToken)
		, bRegistrationSucceeded(bInRegistrationSucceeded)
	{
	}

	uint64 Token = 0;
	bool bRegistrationSucceeded = false;
	friend class FSeinCanonicalStateRecipeRegistry;
};

/**
 * Immutable, registration-order-independent recipe view.
 *
 * The snapshot retains no UClass, CDO, callback, or module vtable. Each entry
 * carries a private exact-generation token; declaration/materialization fails
 * closed if that provider generation unregisters after the freeze.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalStateRecipeSnapshot
{
public:
	bool IsValid() const { return Data.IsValid(); }
	int32 GetRecipeCount() const;
	const FString& GetCanonicalManifest() const;
	FGuid GetContractDigest() const;
	TConstArrayView<FSeinCanonicalStateRecipeDescriptor> GetRecipes() const;

private:
	struct FData;
	TSharedPtr<const FData, ESPMode::ThreadSafe> Data;
	friend class FSeinCanonicalStateRecipeRegistry;
};

/**
 * Process registry for topology-neutral canonical-state recipe classes.
 *
 * Registrations store class paths rather than executable captures. Concrete
 * classes are resolved only on the invoking game-thread stack, their const CDO
 * is called synchronously, and no UObject or Blueprint VM state escapes.
 */
class SEINARTSCOREENTITY_API FSeinCanonicalStateRecipeRegistry
{
public:
	static constexpr int32 MaxRegisteredRecipes = 1024;
	static constexpr int32 MaxReloadClaimsPerRecipe = 64;
	static constexpr int32 MaxSlotsPerRecipe = 256;
	static constexpr int32 MaxTotalSlots = 4096;
	static constexpr int32 MaxContributorIDChars = 128;
	static constexpr int32 MaxRecipeClassPathChars = 1024;
	static constexpr int32 MaxDynamicSchemasPerSlot = 256;
	static constexpr int32 MaxAllowedNamesPerSlot = 4096;
	static constexpr int32 MaxSlotRecursionDepth = 128;
	static constexpr int32 MaxSlotEncodedBytes = 64 * 1024 * 1024;
	static constexpr int32 MaxSlotAggregateElements = 1024 * 1024;

	/**
	 * Register a concrete native or Blueprint recipe class for future freezes.
	 * Exact duplicate claims from the same owner support module reload; any
	 * normal validation, capacity, or semantic-collision failure returns an
	 * invalid handle carrying a poison lease. Store that handle for the module
	 * generation lifetime so Freeze fails closed until it unloads. Game-thread
	 * only.
	 */
	static FSeinCanonicalStateRecipeRegistrationHandle Register(
		FName OwnerModuleID,
		const FSoftClassPath& RecipeClassPath,
		FString* OutError = nullptr);

	/** Convenience overload; the registry still stores only the exact class path. */
	static FSeinCanonicalStateRecipeRegistrationHandle RegisterClass(
		FName OwnerModuleID,
		TSubclassOf<USeinCanonicalStateRecipe> RecipeClass,
		FString* OutError = nullptr);

	/** Unregister one exact provider generation. Game-thread only. */
	static bool Unregister(
		FSeinCanonicalStateRecipeRegistrationHandle& Handle);

	/**
	 * Freeze newest exact claims in canonical contributor-ID order. Fails
	 * while any live module generation retains a failed registration claim.
	 * The digest covers ID, revisions, and exact class path, never owner,
	 * registration token, load address, or registration order.
	 */
	static FSeinCanonicalStateRecipeSnapshot Freeze(
		FString* OutError = nullptr);

	/** Execute and validate only the declaration phase. Transactional outputs. */
	static bool DeclareFrozenRecipes(
		const FSeinCanonicalStateRecipeSnapshot& Snapshot,
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeDeclaration>& OutDeclarations,
		FString& OutError);

	/**
	 * Execute and validate tick-zero materialization against prior declarations.
	 * This is never a checkpoint-restore callback. Transactional outputs.
	 */
	static bool MaterializeFrozenRecipes(
		const FSeinCanonicalStateRecipeSnapshot& Snapshot,
		const FSeinMatchSettings& MatchSettings,
		TConstArrayView<FSeinCanonicalStateRecipeDeclaration> Declarations,
		TArray<FSeinCanonicalStateRecipeMaterialization>& OutMaterializations,
		FString& OutError);

	static int32 GetRegisteredRecipeCount();

	/** ASCII-lowercase identity used for collision checks and canonical ordering. */
	static FString CanonicalContributorID(FName StableContributorID);

private:
	static bool UnregisterToken(uint64 Token);
	friend class FSeinCanonicalStateRecipeRegistrationHandle;
};
