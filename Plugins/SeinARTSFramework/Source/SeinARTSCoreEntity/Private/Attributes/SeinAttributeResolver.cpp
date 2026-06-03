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
#include "UObject/UnrealType.h"

// ---------------------------------------------------------------------------
// Property cache
// ---------------------------------------------------------------------------

namespace SeinAttributeResolverPrivate
{
	/** Cache key: (StructType, FieldName) -> FProperty* */
	static TMap<TPair<UScriptStruct*, FName>, FProperty*> PropertyCache;

	/** Critical section for thread-safe cache access. */
	static FCriticalSection CacheLock;
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

	const TPair<UScriptStruct*, FName> Key(StructType, FieldName);

	// Check cache first
	{
		FScopeLock Lock(&SeinAttributeResolverPrivate::CacheLock);
		if (FProperty** Found = SeinAttributeResolverPrivate::PropertyCache.Find(Key))
		{
			return *Found;
		}
	}

	// Look up via UE reflection
	FProperty* Property = StructType->FindPropertyByName(FieldName);

	// Cache the result (including nullptr for negative lookups)
	{
		FScopeLock Lock(&SeinAttributeResolverPrivate::CacheLock);
		SeinAttributeResolverPrivate::PropertyCache.Add(Key, Property);
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
