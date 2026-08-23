/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentLiveTuning.h
 * @brief   Deterministic property patches used by editor-driven PIE tuning.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "StructUtils/InstancedStruct.h"
#include "SeinComponentLiveTuning.generated.h"

class FProperty;
class USeinAbility;
class USeinEntityComponent;

/** Which Unreal authoring layer produced a live-tuning command. */
UENUM()
enum class ESeinComponentLiveTuningScope : uint8
{
	/** A Blueprint class-default edit. Applies to non-overridden entities of the exact class. */
	ActorClass,
	/** A PIE actor-instance edit. Applies only to the actor's simulation entity. */
	Entity
};

/** One reflected step from a component payload root to an edited value. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinComponentPropertyPathSegment
{
	GENERATED_BODY()

	/** Exact reflected property name. String avoids an open-ended FName wire catalog. */
	UPROPERTY()
	FString PropertyName;

	/** Dynamic-array element index, or INDEX_NONE when the property itself is addressed. */
	UPROPERTY()
	int32 ArrayIndex = INDEX_NONE;

	bool operator==(const FSeinComponentPropertyPathSegment& Other) const
	{
		return PropertyName == Other.PropertyName && ArrayIndex == Other.ArrayIndex;
	}
};

/** How an entity-scoped edit changes its transient instance-override state. */
UENUM()
enum class ESeinComponentInstanceOverrideOperation : uint8
{
	/** Class-scoped patch, or no change to instance-override ownership. */
	None,
	/** The PIE instance now owns this property value. */
	Set,
	/** The PIE instance was reset to the current class default. */
	Clear
};

/** One leaf (or indivisible container) value to apply without replacing its component. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinComponentPropertyPatch
{
	GENERATED_BODY()

	/** Exact UScriptStruct object path for the component storage. */
	UPROPERTY()
	FString ComponentTypePath;

	/** Reflected path within the component. Empty paths are invalid. */
	UPROPERTY()
	TArray<FSeinComponentPropertyPathSegment> PropertyPath;

	/** FProperty canonical text exported by the authoritative editor. */
	UPROPERTY()
	FString ExportedValue;

	UPROPERTY()
	ESeinComponentInstanceOverrideOperation InstanceOverrideOperation =
		ESeinComponentInstanceOverrideOperation::None;
};

/** ActorClass scope: explicit per-class values for one derived class default
 *  the editor could observe when the edit happened. An inheriting class
 *  carries the new value; an overriding class carries a pin of its own
 *  current value so the ancestor overlay can never clobber it. */
struct SEINARTSCOREENTITY_API FSeinComponentLiveTuningClassEntry
{
	FString ActorClassPath;
	TArray<FSeinComponentPropertyPatch> Patches;
};

/** Process-local decoded request used by editor publication and terminal apply. */
struct SEINARTSCOREENTITY_API FSeinComponentLiveTuningRequest
{
	ESeinComponentLiveTuningScope Scope =
		ESeinComponentLiveTuningScope::Entity;
	FSeinEntityHandle TargetEntity;
	FString ActorClassPath;
	TArray<FSeinComponentPropertyPatch> Patches;
	/** ActorClass scope only. Every loaded derived class default, each with its
	 *  own explicit values (see FSeinComponentLiveTuningClassEntry). The sim
	 *  resolves an entity's effective class overlay nearest-derived-first
	 *  along its static class chain, so a derived class the editor could NOT
	 *  observe (unloaded at edit time) falls through to the closest ancestor
	 *  record — the same value every peer computes. The editor resolves the
	 *  hierarchy because only it can observe live class defaults; the sim
	 *  never reads a class default to decide inheritance. Sorted by path,
	 *  unique, never containing ActorClassPath. */
	TArray<FSeinComponentLiveTuningClassEntry> DerivedClassEntries;
};

/** Registered wire payload for SeinARTS.Command.Type.Editor.ComponentPropertyPatch.
 * Open-ended reflected names/text are encoded as canonical UTF-8 bytes because
 * the command protocol deliberately forbids FString and uncatalogued FName. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinComponentLiveTuningCommandPayload
{
	GENERATED_BODY()

	UPROPERTY()
	ESeinComponentLiveTuningScope Scope = ESeinComponentLiveTuningScope::Entity;

	/** Used only by Entity scope. Duplicated from the common envelope deliberately. */
	UPROPERTY()
	FSeinEntityHandle TargetEntity;

	/** Canonical bounded encoding of actor-class path and property patches. */
	UPROPERTY()
	TArray<uint8> EncodedPatchData;
};

/** Canonical state for the latest class-default value of one reflected property. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinComponentClassDefaultPatchRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FString ActorClassPath;

	UPROPERTY()
	FSeinComponentPropertyPatch Patch;
};

/** Canonical evidence that one entity owns an authored or transient instance override. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinComponentEntityOverrideRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinEntityHandle Entity;

	UPROPERTY()
	FString ComponentTypePath;

	UPROPERTY()
	TArray<FSeinComponentPropertyPathSegment> PropertyPath;
};

/** Authored portion of FSeinAbilityComponent::GrantedAbilities for one entity.
 * The live component also contains effect/runtime-granted classes, so this
 * separate canonical baseline is required for property-safe reconciliation. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinComponentAuthoredAbilityGrantRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinEntityHandle Entity;

	/** Multiset in designer-authored order; duplicates are distinct grants. */
	UPROPERTY()
	TArray<TSubclassOf<USeinAbility>> AuthoredAbilities;
};

/** Resolve a patch path against component memory. */
SEINARTSCOREENTITY_API bool SeinResolveComponentPropertyPath(
	const UScriptStruct* ComponentType,
	void* ComponentMemory,
	TConstArrayView<FSeinComponentPropertyPathSegment> Path,
	FProperty*& OutProperty,
	void*& OutValue,
	FString& OutError);

/** Const overload of SeinResolveComponentPropertyPath. */
SEINARTSCOREENTITY_API bool SeinResolveComponentPropertyPath(
	const UScriptStruct* ComponentType,
	const void* ComponentMemory,
	TConstArrayView<FSeinComponentPropertyPathSegment> Path,
	const FProperty*& OutProperty,
	const void*& OutValue,
	FString& OutError);

/** Build stable leaf/container patches for every authored difference. */
SEINARTSCOREENTITY_API bool SeinBuildComponentPropertyPatches(
	const TArray<FInstancedStruct>& Before,
	const TArray<FInstancedStruct>& After,
	TArray<FSeinComponentPropertyPatch>& OutPatches,
	FString& OutError);

/** Stable identity used by class defaults and per-entity override sets. */
SEINARTSCOREENTITY_API FString SeinMakeComponentPropertyPatchKey(
	const FString& ComponentTypePath,
	TConstArrayView<FSeinComponentPropertyPathSegment> Path);

/** Encode/decode the wire-safe, canonical UTF-8 patch body. Scope and target
 * remain ordinary fixed-layout fields on the registered command payload. */
SEINARTSCOREENTITY_API bool SeinEncodeComponentLiveTuningRequest(
	const FSeinComponentLiveTuningRequest& Request,
	FSeinComponentLiveTuningCommandPayload& OutPayload,
	FString& OutError);
SEINARTSCOREENTITY_API bool SeinDecodeComponentLiveTuningRequest(
	const FSeinComponentLiveTuningCommandPayload& Payload,
	FSeinComponentLiveTuningRequest& OutRequest,
	FString& OutError);

#if WITH_EDITOR
/** Editor-only bridge. CoreEntity publishes edits; Net owns authenticated ingress. */
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FSeinComponentLiveTuningEditorRequest,
	const USeinEntityComponent&,
	const FSeinComponentLiveTuningRequest&);

SEINARTSCOREENTITY_API FSeinComponentLiveTuningEditorRequest&
	SeinOnComponentLiveTuningEditorRequest();
#endif
