/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentLiveTuning.cpp
 */

#include "Simulation/SeinComponentLiveTuning.h"

#include "Components/SeinAbilityComponent.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FResolvedValue
	{
		const FProperty* Property = nullptr;
		const void* Value = nullptr;
	};

	bool ResolvePathInternal(
		const UScriptStruct* ComponentType,
		const void* ComponentMemory,
		TConstArrayView<FSeinComponentPropertyPathSegment> Path,
		FResolvedValue& Out,
		FString& OutError)
	{
		Out = {};
		OutError.Reset();
		if (!ComponentType || !ComponentMemory || Path.IsEmpty())
		{
			OutError = TEXT("Component type, memory, and a non-empty property path are required.");
			return false;
		}

		const UStruct* OwnerStruct = ComponentType;
		const void* OwnerMemory = ComponentMemory;
		for (int32 SegmentIndex = 0; SegmentIndex < Path.Num(); ++SegmentIndex)
		{
			const FSeinComponentPropertyPathSegment& Segment = Path[SegmentIndex];
			if (Segment.PropertyName.IsEmpty())
			{
				OutError = TEXT("A property-path segment has an empty name.");
				return false;
			}
			const FProperty* Property = FindFProperty<FProperty>(
				OwnerStruct, FName(*Segment.PropertyName));
			if (!Property || Property->GetName() != Segment.PropertyName)
			{
				OutError = FString::Printf(
					TEXT("Property '%s' does not exist on '%s'."),
					*Segment.PropertyName, *GetNameSafe(OwnerStruct));
				return false;
			}

			const void* Value = Property->ContainerPtrToValuePtr<void>(OwnerMemory);
			const FProperty* AddressedProperty = Property;
			if (Segment.ArrayIndex != INDEX_NONE)
			{
				const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
				if (!ArrayProperty || Segment.ArrayIndex < 0)
				{
					OutError = FString::Printf(
						TEXT("Property '%s' is not a dynamic array."), *Segment.PropertyName);
					return false;
				}
				FScriptArrayHelper Array(ArrayProperty, Value);
				if (!Array.IsValidIndex(Segment.ArrayIndex))
				{
					OutError = FString::Printf(
						TEXT("Array index %d is invalid for '%s' (Num=%d)."),
						Segment.ArrayIndex, *Segment.PropertyName, Array.Num());
					return false;
				}
				AddressedProperty = ArrayProperty->Inner;
				Value = Array.GetRawPtr(Segment.ArrayIndex);
			}

			if (SegmentIndex == Path.Num() - 1)
			{
				Out.Property = AddressedProperty;
				Out.Value = Value;
				return true;
			}

			const FStructProperty* StructProperty =
				CastField<FStructProperty>(AddressedProperty);
			if (!StructProperty || !StructProperty->Struct)
			{
				OutError = FString::Printf(
					TEXT("Property '%s' is not a struct but the path continues."),
					*Segment.PropertyName);
				return false;
			}
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				const FInstancedStruct& DynamicValue =
					*static_cast<const FInstancedStruct*>(Value);
				if (!DynamicValue.IsValid())
				{
					OutError = FString::Printf(
						TEXT("Instanced-struct property '%s' has no concrete value."),
						*Segment.PropertyName);
					return false;
				}
				OwnerStruct = DynamicValue.GetScriptStruct();
				OwnerMemory = DynamicValue.GetMemory();
			}
			else
			{
				OwnerStruct = StructProperty->Struct;
				OwnerMemory = Value;
			}
		}
		return false;
	}

	bool ExportPatch(
		const UScriptStruct& ComponentType,
		const FProperty& Property,
		const void* AfterValue,
		TConstArrayView<FSeinComponentPropertyPathSegment> Path,
		TArray<FSeinComponentPropertyPatch>& OutPatches,
		FString& OutError)
	{
		FString Exported;
		Property.ExportText_Direct(
			Exported, AfterValue, nullptr, nullptr, PPF_None);
		if (Exported.Len() > 64 * 1024)
		{
			OutError = FString::Printf(
				TEXT("Edited value '%s' exceeds the live-tuning value bound."),
				*Property.GetName());
			return false;
		}
		FSeinComponentPropertyPatch& Patch = OutPatches.AddDefaulted_GetRef();
		Patch.ComponentTypePath = ComponentType.GetPathName();
		Patch.PropertyPath.Append(Path.GetData(), Path.Num());
		Patch.ExportedValue = MoveTemp(Exported);
		return true;
	}

	bool StructContainsRuntimeOnlyState(const UStruct& StructType)
	{
		for (TFieldIterator<FProperty> It(&StructType); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property
				|| Property->HasAnyPropertyFlags(
					CPF_Transient | CPF_EditorOnly | CPF_Deprecated
					| CPF_SkipSerialization))
			{
				continue;
			}
			if (!Property->HasAnyPropertyFlags(CPF_Edit))
			{
				return true;
			}
		}
		return false;
	}

	bool DiffStruct(
		const UScriptStruct& ComponentType,
		const UStruct& StructType,
		const void* BeforeMemory,
		const void* AfterMemory,
		TArray<FSeinComponentPropertyPathSegment>& Path,
		TArray<FSeinComponentPropertyPatch>& OutPatches,
		FString& OutError,
		int32 Depth)
	{
		if (Depth > 64)
		{
			OutError = TEXT("Component property recursion exceeds 64 levels.");
			return false;
		}
		for (TFieldIterator<FProperty> It(&StructType); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property
				|| Property->HasAnyPropertyFlags(
					CPF_Transient | CPF_EditorOnly | CPF_Deprecated | CPF_SkipSerialization))
			{
				continue;
			}
			const void* BeforeValue = Property->ContainerPtrToValuePtr<void>(BeforeMemory);
			const void* AfterValue = Property->ContainerPtrToValuePtr<void>(AfterMemory);
			if (Property->Identical(BeforeValue, AfterValue, PPF_None))
			{
				continue;
			}

			FSeinComponentPropertyPathSegment Segment;
			Segment.PropertyName = Property->GetName();
			Path.Add(Segment);

			bool bSucceeded = true;
			if (Property->ArrayDim == 1)
			{
				if (const FStructProperty* StructProperty =
					CastField<FStructProperty>(Property))
				{
					if (StructProperty->Struct
						== FInstancedStruct::StaticStruct())
					{
						const FInstancedStruct& BeforeDynamic =
							*static_cast<const FInstancedStruct*>(BeforeValue);
						const FInstancedStruct& AfterDynamic =
							*static_cast<const FInstancedStruct*>(AfterValue);
						if (BeforeDynamic.IsValid() && AfterDynamic.IsValid()
							&& BeforeDynamic.GetScriptStruct()
								== AfterDynamic.GetScriptStruct())
						{
							const int32 PatchCountBefore = OutPatches.Num();
							bSucceeded = DiffStruct(
								ComponentType,
								*AfterDynamic.GetScriptStruct(),
								BeforeDynamic.GetMemory(),
								AfterDynamic.GetMemory(), Path, OutPatches,
								OutError, Depth + 1);
							// A native custom struct may compare unequal without exposing
							// reflected leaves. Preserve support by treating that one
							// FInstancedStruct property as the indivisible authored field.
							if (bSucceeded && OutPatches.Num() == PatchCountBefore)
							{
								bSucceeded = ExportPatch(
									ComponentType, *Property, AfterValue,
									Path, OutPatches, OutError);
							}
						}
						else
						{
							// Changing the selected concrete type is one authored-field
							// replacement. Its old inner path no longer exists.
							bSucceeded = ExportPatch(
								ComponentType, *Property, AfterValue,
								Path, OutPatches, OutError);
						}
					}
					else
					{
						bSucceeded = DiffStruct(
							ComponentType, *StructProperty->Struct,
							BeforeValue, AfterValue, Path, OutPatches,
							OutError, Depth + 1);
					}
				}
				else if (const FArrayProperty* ArrayProperty =
					CastField<FArrayProperty>(Property))
				{
					FScriptArrayHelper BeforeArray(ArrayProperty, BeforeValue);
					FScriptArrayHelper AfterArray(ArrayProperty, AfterValue);
					const bool bIsAbilityGrantList =
						&ComponentType == FSeinAbilityComponent::StaticStruct()
						&& Property->GetFName()
							== GET_MEMBER_NAME_CHECKED(
								FSeinAbilityComponent, GrantedAbilities);
					if (bIsAbilityGrantList)
					{
						// The ability lifecycle owns this authored multiset and reconciles
						// it against effect/runtime grants as one transaction.
						bSucceeded = ExportPatch(
							ComponentType, *Property, AfterValue,
							Path, OutPatches, OutError);
					}
					else if (BeforeArray.Num() != AfterArray.Num())
					{
						const FStructProperty* InnerStruct =
							CastField<FStructProperty>(ArrayProperty->Inner);
						if (InnerStruct && InnerStruct->Struct
							&& StructContainsRuntimeOnlyState(*InnerStruct->Struct))
						{
							OutError = FString::Printf(
								TEXT("Resizing '%s' changes array topology whose elements contain runtime-only state; stop PIE or use a component-owned live-tuning handler."),
								*Property->GetName());
							bSucceeded = false;
						}
						else
						{
							// Pure-authored containers can be replaced as one field. No
							// unrelated canonical runtime state lives inside their elements.
							bSucceeded = ExportPatch(
								ComponentType, *Property, AfterValue,
								Path, OutPatches, OutError);
						}
					}
					else
					{
						// With stable element topology, descend into struct elements so a
						// weapon Range edit cannot overwrite its live cooldown/magazine.
						for (int32 ArrayIndex = 0;
							bSucceeded && ArrayIndex < AfterArray.Num(); ++ArrayIndex)
						{
							const void* BeforeElement = BeforeArray.GetRawPtr(ArrayIndex);
							const void* AfterElement = AfterArray.GetRawPtr(ArrayIndex);
							if (ArrayProperty->Inner->Identical(
								BeforeElement, AfterElement, PPF_None))
							{
								continue;
							}
							Path.Last().ArrayIndex = ArrayIndex;
							if (const FStructProperty* InnerStruct =
								CastField<FStructProperty>(ArrayProperty->Inner))
							{
								bSucceeded = DiffStruct(
									ComponentType, *InnerStruct->Struct,
									BeforeElement, AfterElement, Path,
									OutPatches, OutError, Depth + 1);
							}
							else
							{
								bSucceeded = ExportPatch(
									ComponentType, *ArrayProperty->Inner,
									AfterElement, Path, OutPatches, OutError);
							}
							Path.Last().ArrayIndex = INDEX_NONE;
						}
					}
				}
				else
				{
					bSucceeded = ExportPatch(
						ComponentType, *Property, AfterValue,
						Path, OutPatches, OutError);
				}
			}
			else
			{
				// Static arrays are one reflected property. Replacing just this property
				// is still narrower than replacing the containing sim component.
				bSucceeded = ExportPatch(
					ComponentType, *Property, AfterValue,
					Path, OutPatches, OutError);
			}

			Path.Pop();
			if (!bSucceeded)
			{
				return false;
			}
		}
		return true;
	}

	const FInstancedStruct* FindEntry(
		const TArray<FInstancedStruct>& Entries,
		const UScriptStruct* Type)
	{
		for (const FInstancedStruct& Entry : Entries)
		{
			if (Entry.IsValid() && Entry.GetScriptStruct() == Type)
			{
				return &Entry;
			}
		}
		return nullptr;
	}
}

bool SeinResolveComponentPropertyPath(
	const UScriptStruct* ComponentType,
	void* ComponentMemory,
	TConstArrayView<FSeinComponentPropertyPathSegment> Path,
	FProperty*& OutProperty,
	void*& OutValue,
	FString& OutError)
{
	FResolvedValue Resolved;
	if (!ResolvePathInternal(
		ComponentType, ComponentMemory, Path, Resolved, OutError))
	{
		OutProperty = nullptr;
		OutValue = nullptr;
		return false;
	}
	OutProperty = const_cast<FProperty*>(Resolved.Property);
	OutValue = const_cast<void*>(Resolved.Value);
	return true;
}

bool SeinResolveComponentPropertyPath(
	const UScriptStruct* ComponentType,
	const void* ComponentMemory,
	TConstArrayView<FSeinComponentPropertyPathSegment> Path,
	const FProperty*& OutProperty,
	const void*& OutValue,
	FString& OutError)
{
	FResolvedValue Resolved;
	if (!ResolvePathInternal(
		ComponentType, ComponentMemory, Path, Resolved, OutError))
	{
		OutProperty = nullptr;
		OutValue = nullptr;
		return false;
	}
	OutProperty = Resolved.Property;
	OutValue = Resolved.Value;
	return true;
}

bool SeinBuildComponentPropertyPatches(
	const TArray<FInstancedStruct>& Before,
	const TArray<FInstancedStruct>& After,
	TArray<FSeinComponentPropertyPatch>& OutPatches,
	FString& OutError)
{
	OutPatches.Reset();
	OutError.Reset();
	for (const FInstancedStruct& BeforeEntry : Before)
	{
		if (!BeforeEntry.IsValid())
		{
			continue;
		}
		if (!FindEntry(After, BeforeEntry.GetScriptStruct()))
		{
			OutError = FString::Printf(
				TEXT("Removing component type '%s' is a structural edit and cannot be live tuned."),
				*BeforeEntry.GetScriptStruct()->GetPathName());
			OutPatches.Reset();
			return false;
		}
	}
	for (const FInstancedStruct& AfterEntry : After)
	{
		if (!AfterEntry.IsValid())
		{
			continue;
		}
		const UScriptStruct* Type = AfterEntry.GetScriptStruct();
		const FInstancedStruct* BeforeEntry = FindEntry(Before, Type);
		if (!BeforeEntry)
		{
			OutError = FString::Printf(
				TEXT("Adding component type '%s' is a structural edit and cannot be live tuned."),
				*Type->GetPathName());
			OutPatches.Reset();
			return false;
		}
		TArray<FSeinComponentPropertyPathSegment> Path;
		if (!DiffStruct(
			*Type, *Type, BeforeEntry->GetMemory(), AfterEntry.GetMemory(),
			Path, OutPatches, OutError, 0))
		{
			OutPatches.Reset();
			return false;
		}
	}
	return true;
}

FString SeinMakeComponentPropertyPatchKey(
	const FString& ComponentTypePath,
	TConstArrayView<FSeinComponentPropertyPathSegment> Path)
{
	FString Result = ComponentTypePath;
	for (const FSeinComponentPropertyPathSegment& Segment : Path)
	{
		Result.AppendChar(TEXT('|'));
		Result.Appendf(TEXT("%d:%s"), Segment.PropertyName.Len(), *Segment.PropertyName);
		if (Segment.ArrayIndex != INDEX_NONE)
		{
			Result.Appendf(TEXT("[%d]"), Segment.ArrayIndex);
		}
	}
	return Result;
}

namespace
{
	constexpr int32 MaxEncodedLiveTuningBytes = 64 * 1024;

	void WriteUInt32(TArray<uint8>& Out, uint32 Value)
	{
		Out.Add(static_cast<uint8>(Value));
		Out.Add(static_cast<uint8>(Value >> 8));
		Out.Add(static_cast<uint8>(Value >> 16));
		Out.Add(static_cast<uint8>(Value >> 24));
	}

	bool WriteUtf8(TArray<uint8>& Out, const FString& Value, FString& OutError)
	{
		FTCHARToUTF8 Utf8(*Value, Value.Len());
		if (Utf8.Length() < 0 || Utf8.Length() > MaxEncodedLiveTuningBytes
			|| Out.Num() > MaxEncodedLiveTuningBytes - Utf8.Length() - 4)
		{
			OutError = TEXT("Live-tuning UTF-8 value exceeds the wire bound.");
			return false;
		}
		WriteUInt32(Out, static_cast<uint32>(Utf8.Length()));
		Out.Append(
			reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return true;
	}

	bool ReadUInt32(
		TConstArrayView<uint8> Bytes, int32& Cursor, uint32& OutValue)
	{
		if (Cursor < 0 || Cursor > Bytes.Num() - 4) return false;
		OutValue = static_cast<uint32>(Bytes[Cursor])
			| (static_cast<uint32>(Bytes[Cursor + 1]) << 8)
			| (static_cast<uint32>(Bytes[Cursor + 2]) << 16)
			| (static_cast<uint32>(Bytes[Cursor + 3]) << 24);
		Cursor += 4;
		return true;
	}

	bool ReadUtf8(
		TConstArrayView<uint8> Bytes,
		int32& Cursor,
		FString& OutValue,
		FString& OutError)
	{
		uint32 Length = 0;
		if (!ReadUInt32(Bytes, Cursor, Length)
			|| Length > static_cast<uint32>(MaxEncodedLiveTuningBytes)
			|| Cursor > Bytes.Num() - static_cast<int32>(Length))
		{
			OutError = TEXT("Live-tuning UTF-8 frame is truncated or oversized.");
			return false;
		}
		if (Length == 0)
		{
			OutValue.Reset();
			return true;
		}
		const ANSICHAR* Begin = reinterpret_cast<const ANSICHAR*>(
			Bytes.GetData() + Cursor);
		FUTF8ToTCHAR Converted(Begin, static_cast<int32>(Length));
		OutValue = FString(Converted.Length(), Converted.Get());
		FTCHARToUTF8 RoundTrip(*OutValue, OutValue.Len());
		if (RoundTrip.Length() != static_cast<int32>(Length)
			|| FMemory::Memcmp(
				RoundTrip.Get(), Begin, static_cast<SIZE_T>(Length)) != 0)
		{
			OutError = TEXT("Live-tuning string is not canonical UTF-8.");
			return false;
		}
		Cursor += static_cast<int32>(Length);
		return true;
	}
}

namespace
{
	bool WritePatchList(
		TArray<uint8>& Bytes,
		const TArray<FSeinComponentPropertyPatch>& Patches,
		FString& OutError)
	{
		if (Patches.IsEmpty() || Patches.Num() > 256)
		{
			OutError = TEXT("Live-tuning patch list requires 1..256 patches.");
			return false;
		}
		WriteUInt32(Bytes, static_cast<uint32>(Patches.Num()));
		for (const FSeinComponentPropertyPatch& Patch : Patches)
		{
			if (Patch.PropertyPath.IsEmpty() || Patch.PropertyPath.Num() > 64
				|| !WriteUtf8(Bytes, Patch.ComponentTypePath, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT("Live-tuning property path requires 1..64 segments.");
				}
				return false;
			}
			WriteUInt32(Bytes, static_cast<uint32>(Patch.PropertyPath.Num()));
			for (const FSeinComponentPropertyPathSegment& Segment : Patch.PropertyPath)
			{
				if (!WriteUtf8(Bytes, Segment.PropertyName, OutError)) return false;
				WriteUInt32(Bytes, static_cast<uint32>(Segment.ArrayIndex));
			}
			if (!WriteUtf8(Bytes, Patch.ExportedValue, OutError)) return false;
			Bytes.Add(static_cast<uint8>(Patch.InstanceOverrideOperation));
			if (Bytes.Num() > MaxEncodedLiveTuningBytes)
			{
				OutError = TEXT("Live-tuning request exceeds 64 KiB.");
				return false;
			}
		}
		return true;
	}

	bool ReadPatchList(
		TConstArrayView<uint8> Bytes,
		int32& Cursor,
		TArray<FSeinComponentPropertyPatch>& OutPatches,
		FString& OutError)
	{
		uint32 PatchCount = 0;
		if (!ReadUInt32(Bytes, Cursor, PatchCount)
			|| PatchCount == 0 || PatchCount > 256)
		{
			if (OutError.IsEmpty()) OutError = TEXT("Invalid live-tuning patch count.");
			return false;
		}
		OutPatches.Reset();
		OutPatches.Reserve(static_cast<int32>(PatchCount));
		for (uint32 PatchIndex = 0; PatchIndex < PatchCount; ++PatchIndex)
		{
			FSeinComponentPropertyPatch& Patch = OutPatches.AddDefaulted_GetRef();
			uint32 SegmentCount = 0;
			if (!ReadUtf8(Bytes, Cursor, Patch.ComponentTypePath, OutError)
				|| !ReadUInt32(Bytes, Cursor, SegmentCount)
				|| SegmentCount == 0 || SegmentCount > 64)
			{
				if (OutError.IsEmpty()) OutError = TEXT("Invalid live-tuning path count.");
				return false;
			}
			Patch.PropertyPath.Reserve(static_cast<int32>(SegmentCount));
			for (uint32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
			{
				FSeinComponentPropertyPathSegment& Segment =
					Patch.PropertyPath.AddDefaulted_GetRef();
				uint32 ArrayIndex = 0;
				if (!ReadUtf8(Bytes, Cursor, Segment.PropertyName, OutError)
					|| !ReadUInt32(Bytes, Cursor, ArrayIndex)) return false;
				Segment.ArrayIndex = static_cast<int32>(ArrayIndex);
			}
			if (!ReadUtf8(Bytes, Cursor, Patch.ExportedValue, OutError)
				|| Cursor >= Bytes.Num())
			{
				if (OutError.IsEmpty()) OutError = TEXT("Truncated live-tuning operation.");
				return false;
			}
			Patch.InstanceOverrideOperation =
				static_cast<ESeinComponentInstanceOverrideOperation>(Bytes[Cursor++]);
		}
		return true;
	}
}

bool SeinEncodeComponentLiveTuningRequest(
	const FSeinComponentLiveTuningRequest& Request,
	FSeinComponentLiveTuningCommandPayload& OutPayload,
	FString& OutError)
{
	OutPayload = {};
	OutError.Reset();
	if (Request.DerivedClassEntries.Num() > 256)
	{
		OutError = TEXT("Live-tuning request allows at most 256 derived class entries.");
		return false;
	}
	OutPayload.Scope = Request.Scope;
	OutPayload.TargetEntity = Request.TargetEntity;
	TArray<uint8>& Bytes = OutPayload.EncodedPatchData;
	if (!WriteUtf8(Bytes, Request.ActorClassPath, OutError)) return false;
	if (!WritePatchList(Bytes, Request.Patches, OutError)) return false;
	WriteUInt32(Bytes, static_cast<uint32>(Request.DerivedClassEntries.Num()));
	for (const FSeinComponentLiveTuningClassEntry& Entry : Request.DerivedClassEntries)
	{
		if (!WriteUtf8(Bytes, Entry.ActorClassPath, OutError)
			|| !WritePatchList(Bytes, Entry.Patches, OutError))
		{
			return false;
		}
	}
	if (Bytes.Num() > MaxEncodedLiveTuningBytes)
	{
		OutError = TEXT("Live-tuning request exceeds 64 KiB.");
		return false;
	}
	return true;
}

bool SeinDecodeComponentLiveTuningRequest(
	const FSeinComponentLiveTuningCommandPayload& Payload,
	FSeinComponentLiveTuningRequest& OutRequest,
	FString& OutError)
{
	OutRequest = {};
	OutError.Reset();
	if (Payload.EncodedPatchData.IsEmpty()
		|| Payload.EncodedPatchData.Num() > MaxEncodedLiveTuningBytes)
	{
		OutError = TEXT("Live-tuning command body is empty or oversized.");
		return false;
	}
	OutRequest.Scope = Payload.Scope;
	OutRequest.TargetEntity = Payload.TargetEntity;
	const TConstArrayView<uint8> Bytes(Payload.EncodedPatchData);
	int32 Cursor = 0;
	if (!ReadUtf8(Bytes, Cursor, OutRequest.ActorClassPath, OutError)
		|| !ReadPatchList(Bytes, Cursor, OutRequest.Patches, OutError))
	{
		return false;
	}
	uint32 DerivedClassCount = 0;
	if (!ReadUInt32(Bytes, Cursor, DerivedClassCount)
		|| DerivedClassCount > 256)
	{
		if (OutError.IsEmpty()) OutError = TEXT("Invalid live-tuning derived class count.");
		return false;
	}
	OutRequest.DerivedClassEntries.Reserve(static_cast<int32>(DerivedClassCount));
	for (uint32 DerivedIndex = 0; DerivedIndex < DerivedClassCount; ++DerivedIndex)
	{
		FSeinComponentLiveTuningClassEntry& Entry =
			OutRequest.DerivedClassEntries.AddDefaulted_GetRef();
		if (!ReadUtf8(Bytes, Cursor, Entry.ActorClassPath, OutError)
			|| !ReadPatchList(Bytes, Cursor, Entry.Patches, OutError))
		{
			return false;
		}
	}
	if (Cursor != Bytes.Num())
	{
		OutError = TEXT("Live-tuning command body has trailing bytes.");
		return false;
	}
	return true;
}

#if WITH_EDITOR
FSeinComponentLiveTuningEditorRequest& SeinOnComponentLiveTuningEditorRequest()
{
	static FSeinComponentLiveTuningEditorRequest Delegate;
	return Delegate;
}
#endif
