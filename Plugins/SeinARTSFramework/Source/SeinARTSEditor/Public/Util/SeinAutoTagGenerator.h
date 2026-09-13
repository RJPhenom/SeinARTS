/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinAutoTagGenerator.h
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Generates and migrates identities with their owning asset names.
 * @disclaimer   Generated with assistance from an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UBlueprint;
class USeinARTSCoreSettings;
struct FSeinTagPrefixMapping;

enum class ESeinAutoTagRegenerationOutcome : uint8
{
	Updated,
	AlreadyCurrent,
	SkippedDesignerOwned,
	InvalidBlueprint,
	AutoTagDisabled,
	UnsupportedAsset,
	TagDerivationFailed,
	Collision,
	MigrationRequired,
	EditorBusy,
	AuthoringConflict,
};

/** Detailed outcome for one editor auto-tag regeneration attempt. */
struct SEINARTSEDITOR_API FSeinAutoTagRegenerationResult
{
	ESeinAutoTagRegenerationOutcome Outcome =
		ESeinAutoTagRegenerationOutcome::InvalidBlueprint;
	FGameplayTag DerivedTag;
	FString CollidingAssetPath;
	FText Detail;

	bool WasUpdated() const
	{
		return Outcome == ESeinAutoTagRegenerationOutcome::Updated;
	}

	bool IsFailure() const;
	FText ToUserMessage(const UBlueprint* OwningBP) const;
};

namespace SeinAutoTag
{
	/** Compute a proposed name without registering a tag or changing any file. */
	SEINARTSEDITOR_API FName ProposeTagName(
		const FName& AssetName, const USeinARTSCoreSettings* Settings);

	/** Read the authoritative authored identity, including inherited components. */
	SEINARTSEDITOR_API bool ReadAssetIdentity(
		const UBlueprint* Blueprint, FGameplayTag& OutTag, bool& bOutGenerated);

	/** Assign a new asset's identity after creation succeeds. Does not redirect the inherited identity. */
	SEINARTSEDITOR_API FSeinAutoTagRegenerationResult InitializeNewAssetTag(UBlueprint* Blueprint);

	/** Migrate a generated identity after an asset rename, allowing existing unsaved authoring edits.
	 *  PreviousTag also supports recovery after the field was reset/reinitialized. */
	SEINARTSEDITOR_API FSeinAutoTagRegenerationResult ReconcileGeneratedIdentity(UBlueprint* Blueprint, FName PreviousTag);
	SEINARTSEDITOR_API void RememberGeneratedIdentity(UBlueprint* Blueprint, FGameplayTag Tag);
	SEINARTSEDITOR_API FName LastGeneratedIdentity(const UBlueprint* Blueprint);
	/** Finish queued compilation after the asset rename stack has returned. */
	SEINARTSEDITOR_API void FinishPendingTagMigrations();
	SEINARTSEDITOR_API void ShutdownMigrationSupport();

	/** Explicitly migrate an identity and its entire subtree, retaining redirects for serialized references.
	 *  Existing runtime sessions must be stopped. Existing target tags are never merged. */
	SEINARTSEDITOR_API FSeinAutoTagRegenerationResult RenameAssetTag(
		UBlueprint* Blueprint, FName NewTagName);

    /** Lookup-only compatibility helper. ProposeTagName computes unregistered names without writes;
     *  InitializeNewAssetTag registers only after an asset and writable identity target exist. */
	SEINARTSEDITOR_API FGameplayTag DeriveTagFromAssetName(
		const FName& AssetName,
		const USeinARTSCoreSettings* Settings);

	/** Walk Settings.PrefixCategoryMappings, return the first entry whose
	 *  AssetPrefix matches the leading underscore-delimited segment of
	 *  AssetName. Returns nullptr if no match. */
	SEINARTSEDITOR_API const FSeinTagPrefixMapping* FindMatchingPrefixMapping(
		const FName& AssetName,
		const USeinARTSCoreSettings* Settings);

	/** Register a tag with the gameplay tags manager if it's not already
	 *  present. Idempotent. Safe to call repeatedly. */
	SEINARTSEDITOR_API void RegisterTagIfNeeded(const FGameplayTag& Tag);

	/** Walk the asset registry for any SeinARTS-known asset class (ability,
	 *  effect, entity blueprint) whose stored tag equals `Tag`. Returns the
	 *  first colliding asset's package path, or empty if no collision.
	 *  `IgnorePackagePath` lets the caller exclude its own asset from the
	 *  scan (we don't want a BP to collide with itself on rename). */
	SEINARTSEDITOR_API FString FindCollidingAssetPath(
		const FGameplayTag& Tag,
		const FString& IgnorePackagePath = FString());

    /** Reconcile an identity with its asset name, including remembered generated identities after reset.
     *  Force also permits a designer-owned field to return to generated ownership. */
	SEINARTSEDITOR_API FSeinAutoTagRegenerationResult
		RegenerateAssetTagDetailed(
			UBlueprint* OwningBP,
			bool bForceOverManual);

#if WITH_DEV_AUTOMATION_TESTS
	/** Exercise rollback after the dictionary has rebuilt, before any loaded reference is changed. */
	SEINARTSEDITOR_API FSeinAutoTagRegenerationResult RenameAssetTagWithRefreshFailureForAutomation(
		UBlueprint* Blueprint, FName NewTagName);
	/** Automation-only scoped collision seam. Production regeneration always
	 *  enforces project-wide tag uniqueness. */
	SEINARTSEDITOR_API FSeinAutoTagRegenerationResult
		RegenerateAssetTagDetailedForAutomation(
			UBlueprint* OwningBP,
			bool bForceOverManual,
			const FString& CollisionSearchPackageRoot);
#endif

	/** Compatibility wrapper over `RegenerateAssetTagDetailed`. Returns true
	 *  only when the asset was actually changed. */
	SEINARTSEDITOR_API bool RegenerateAssetTag(
		UBlueprint* OwningBP,
		bool bForceOverManual);

	/** Walk the asset registry for every SeinARTS taggable BP and call
	 *  RegenerateAssetTag. Returns the count of assets updated.
	 *  `bForceOverManual` mirrors the legacy bulk initialization callers. Existing valid identities are retained. */
	SEINARTSEDITOR_API int32 RegenerateAllAssetTags(bool bForceOverManual);
}
