/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinAttributeResolver.cpp
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of attribute field access and modifier resolution.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#include "Attributes/SeinAttributeResolver.h"
#include "UObject/NameTypes.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

// ---------------------------------------------------------------------------
// Property cache
// ---------------------------------------------------------------------------

namespace SeinAttributeResolverPrivate
{
	/**
	 * Weak struct roots make cached reflection safe across reinstancing and
	 * module unload. A stale entry can retain neither the UScriptStruct nor its
	 * native module, and its FProperty pointer is never reachable through a new
	 * struct generation's distinct weak-object identity.
	 */
	static TMap<
		TWeakObjectPtr<UScriptStruct>,
		TMap<FName, FProperty*>> PropertyCache;

	/** Critical section for thread-safe cache access. */
	static FCriticalSection CacheLock;

	/** Mirror of the struct editor's member-variable stem generation
	 *  (FMemberVariableNameHelper::Generate): a display label that is already a
	 *  valid object name is kept verbatim, anything else is slugged through
	 *  MakeObjectNameFromDisplayLabel, and an empty result becomes "MemberVar".
	 *  Applying it to both the requested name and the authored name yields the
	 *  same comparison key whether the authored name is the editor's friendly
	 *  string or the cooked build's internal stem. */
	static FString SanitizeAuthoredName(const FString& Name)
	{
		FString Result;
		if (!Name.IsEmpty())
		{
			Result = FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS)
				? Name
				: MakeObjectNameFromDisplayLabel(Name, NAME_None).GetPlainNameString();
		}
		if (Result.IsEmpty())
		{
			Result = TEXT("MemberVar");
		}
		return Result;
	}
}

// ---------------------------------------------------------------------------
// FindFieldProperty
// ---------------------------------------------------------------------------

FProperty* FSeinAttributeResolver::FindFieldProperty(UScriptStruct* StructType, FName FieldName)
{
	if (!StructType || FieldName.IsNone())
	{
		return nullptr;
	}

	const TWeakObjectPtr<UScriptStruct> StructKey(StructType);

	// Check cache first
	{
		FScopeLock Lock(&SeinAttributeResolverPrivate::CacheLock);
		if (TMap<FName, FProperty*>* StructProperties =
			SeinAttributeResolverPrivate::PropertyCache.Find(StructKey))
		{
			if (FProperty** Found = StructProperties->Find(FieldName))
			{
				return *Found;
			}
		}
	}

	// Look up via UE reflection. Exact internal names first; then the
	// authored (display) name so designer-authored UUserDefinedStruct fields
	// — whose internal names carry a generated `_<index>_<guid>` suffix — can
	// be addressed by the name the designer typed.
	//
	// UUserDefinedStruct::GetAuthoredNameForField returns the raw friendly
	// name in the editor but only the sanitized internal stem in cooked builds
	// (e.g. "Current Health" vs "CurrentHealth"). Comparing both sides through
	// the SAME sanitizer the struct editor used to generate that stem makes the
	// resolution identical in editor and cooked builds — a PIE host and a
	// packaged client must resolve the same property or lockstep diverges.
	FProperty* Property = StructType->FindPropertyByName(FieldName);
	if (!Property)
	{
		const FString Wanted =
			SeinAttributeResolverPrivate::SanitizeAuthoredName(FieldName.ToString());
		for (TFieldIterator<FProperty> It(StructType); It; ++It)
		{
			if (SeinAttributeResolverPrivate::SanitizeAuthoredName(
					StructType->GetAuthoredNameForField(*It)) == Wanted)
			{
				Property = *It;
				break;
			}
		}
	}

	// Cache the result (including nullptr for negative lookups)
	{
		FScopeLock Lock(&SeinAttributeResolverPrivate::CacheLock);
		SeinAttributeResolverPrivate::PropertyCache
			.FindOrAdd(StructKey)
			.Add(FieldName, Property);
	}

	return Property;
}

// ---------------------------------------------------------------------------
// IsFixedPointField
// ---------------------------------------------------------------------------

bool FSeinAttributeResolver::IsFixedPointField(FProperty* Property)
{
	if (!Property)
	{
		return false;
	}

	const FStructProperty* StructProp = CastField<FStructProperty>(Property);
	return StructProp && StructProp->Struct == FFixedPoint::StaticStruct();
}

// ---------------------------------------------------------------------------
// ReadFixedPointField
// ---------------------------------------------------------------------------

FFixedPoint FSeinAttributeResolver::ReadFixedPointField(const void* StructData, UScriptStruct* StructType, FName FieldName)
{
	if (!StructData || !StructType)
	{
		return FFixedPoint::Zero;
	}

	FProperty* Property = FindFieldProperty(StructType, FieldName);
	if (!IsFixedPointField(Property))
	{
		return FFixedPoint::Zero;
	}

	const FFixedPoint* ValuePtr = Property->ContainerPtrToValuePtr<FFixedPoint>(StructData);
	return ValuePtr ? *ValuePtr : FFixedPoint::Zero;
}

// ---------------------------------------------------------------------------
// WriteFixedPointField
// ---------------------------------------------------------------------------

bool FSeinAttributeResolver::WriteFixedPointField(void* StructData, UScriptStruct* StructType, FName FieldName, FFixedPoint Value)
{
	if (!StructData || !StructType)
	{
		return false;
	}

	FProperty* Property = FindFieldProperty(StructType, FieldName);
	if (!IsFixedPointField(Property))
	{
		return false;
	}

	FFixedPoint* ValuePtr = Property->ContainerPtrToValuePtr<FFixedPoint>(StructData);
	if (!ValuePtr)
	{
		return false;
	}

	*ValuePtr = Value;
	return true;
}

// ---------------------------------------------------------------------------
// ResolveModifiers
// ---------------------------------------------------------------------------

FFixedPoint FSeinAttributeResolver::ResolveModifiers(FFixedPoint BaseValue, const TArray<FSeinModifier>& Modifiers)
{
	FFixedPoint SumAdd = FFixedPoint::Zero;
	FFixedPoint ProductMul = FFixedPoint::One;
	bool bHasOverride = false;
	FFixedPoint OverrideValue = FFixedPoint::Zero;

	for (const FSeinModifier& Mod : Modifiers)
	{
		switch (Mod.Operation)
		{
		case ESeinModifierOp::Add:
			SumAdd = SumAdd + Mod.Value;
			break;

		case ESeinModifierOp::Multiply:
			ProductMul = ProductMul * Mod.Value;
			break;

		case ESeinModifierOp::Override:
			bHasOverride = true;
			OverrideValue = Mod.Value;  // Last override wins
			break;
		}
	}

	FFixedPoint Base = bHasOverride ? OverrideValue : BaseValue;
	FFixedPoint Final = (Base + SumAdd) * ProductMul;
	return Final;
}

// ---------------------------------------------------------------------------
// ClearPropertyCache
// ---------------------------------------------------------------------------

void FSeinAttributeResolver::ClearPropertyCache()
{
	FScopeLock Lock(&SeinAttributeResolverPrivate::CacheLock);
	SeinAttributeResolverPrivate::PropertyCache.Empty();
}
