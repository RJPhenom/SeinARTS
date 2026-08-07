/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPoolObjectCodecRegistry.cpp
 */

#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "UObject/TextProperty.h"

#include "Abilities/SeinAbility.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/BlueprintSupport.h"
#include "Brokers/SeinCommandBrokerResolver.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Data/SeinResourceTypes.h"
#include "Data/SeinWorldSnapshot.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/PackageName.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalStatePropertyPolicy.h"
#include "Serialization/SeinStateProviderTransaction.h"
#include "SeinARTSCoreEntityLog.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr uint32 ManifestFormatVersion = 1;
	constexpr uint32 DescriptorFormatVersion = 1;
	constexpr uint32 ReflectedEnvelopeVersion = 2;
	constexpr int32 MaxResourceCostEntries = 4096;
	constexpr int32 MaxResourceTagUtf8Bytes = 1024;
	constexpr int32 MaxReflectedAggregateElements = 65536;
	constexpr int32 MaxReflectedStringUtf8Bytes = 64 * 1024;

	FSeinStructWireLimits MakeReflectedWireLimits()
	{
		FSeinStructWireLimits Limits;
		Limits.MaxBytes =
			FSeinPoolObjectCodecRegistry::MaxStateBytes;
		Limits.MaxAggregateElements =
			MaxReflectedAggregateElements;
		Limits.MaxStringBytes =
			MaxReflectedStringUtf8Bytes;
		Limits.MaxRecursionDepth = 64;
		Limits.MaxNativeAllocationBytes =
			FSeinPoolObjectCodecRegistry::MaxStateBytes;
		return Limits;
	}

	void AppendUInt32(TArray<uint8>& Out, uint32 Value)
	{
		Out.Add(static_cast<uint8>((Value >> 24) & 0xff));
		Out.Add(static_cast<uint8>((Value >> 16) & 0xff));
		Out.Add(static_cast<uint8>((Value >> 8) & 0xff));
		Out.Add(static_cast<uint8>(Value & 0xff));
	}

	void AppendInt64(TArray<uint8>& Out, int64 Value)
	{
		const uint64 Bits = static_cast<uint64>(Value);
		for (int32 Shift = 56; Shift >= 0; Shift -= 8)
		{
			Out.Add(static_cast<uint8>((Bits >> Shift) & 0xff));
		}
	}

	bool ReadUInt32(
		TConstArrayView<uint8> Bytes,
		int32& Cursor,
		uint32& Out)
	{
		if (Cursor < 0 || Bytes.Num() - Cursor < 4)
		{
			return false;
		}
		Out = (static_cast<uint32>(Bytes[Cursor]) << 24)
			| (static_cast<uint32>(Bytes[Cursor + 1]) << 16)
			| (static_cast<uint32>(Bytes[Cursor + 2]) << 8)
			| static_cast<uint32>(Bytes[Cursor + 3]);
		Cursor += 4;
		return true;
	}

	bool ReadInt64(
		TConstArrayView<uint8> Bytes,
		int32& Cursor,
		int64& Out)
	{
		if (Cursor < 0 || Bytes.Num() - Cursor < 8)
		{
			return false;
		}
		uint64 Bits = 0;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Bits = (Bits << 8) | Bytes[Cursor + Index];
		}
		Cursor += 8;
		Out = static_cast<int64>(Bits);
		return true;
	}

	bool AppendResourceCost(
		const FSeinResourceCost& Cost,
		TArray<uint8>& Out,
		FString& OutError)
	{
		if (Cost.Amounts.Num() > MaxResourceCostEntries)
		{
			OutError =
				TEXT("Ability resource-cost state exceeds its entry bound.");
			return false;
		}
		struct FEntry
		{
			FString Tag;
			int64 Amount = 0;
		};
		TArray<FEntry> Entries;
		Entries.Reserve(Cost.Amounts.Num());
		for (const auto& Pair : Cost.Amounts)
		{
			if (!Pair.Key.IsValid())
			{
				OutError =
					TEXT("Ability resource-cost state contains an invalid tag.");
				return false;
			}
			FEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Tag = Pair.Key.ToString();
			Entry.Amount = Pair.Value.Value;
		}
		Entries.Sort([](const FEntry& A, const FEntry& B)
		{
			return A.Tag < B.Tag;
		});
		AppendUInt32(Out, static_cast<uint32>(Entries.Num()));
		for (const FEntry& Entry : Entries)
		{
			const FTCHARToUTF8 Utf8(*Entry.Tag);
			if (Utf8.Length() <= 0
				|| Utf8.Length() > MaxResourceTagUtf8Bytes)
			{
				OutError =
					TEXT("Ability resource tag exceeds its wire bound.");
				return false;
			}
			AppendUInt32(Out, static_cast<uint32>(Utf8.Length()));
			Out.Append(
				reinterpret_cast<const uint8*>(Utf8.Get()),
				Utf8.Length());
			AppendInt64(Out, Entry.Amount);
		}
		return true;
	}

	bool ReadResourceCost(
		TConstArrayView<uint8> Bytes,
		int32& Cursor,
		FSeinResourceCost& OutCost,
		FString& OutError)
	{
		OutCost.Amounts.Reset();
		uint32 Count = 0;
		if (!ReadUInt32(Bytes, Cursor, Count)
			|| Count > MaxResourceCostEntries)
		{
			OutError =
				TEXT("Ability resource-cost entry count is invalid.");
			return false;
		}
		FString PreviousTag;
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			uint32 Utf8Length = 0;
			if (!ReadUInt32(Bytes, Cursor, Utf8Length)
				|| Utf8Length == 0
				|| Utf8Length > MaxResourceTagUtf8Bytes
				|| Utf8Length
					> static_cast<uint32>(Bytes.Num() - Cursor))
			{
				OutError =
					TEXT("Ability resource tag wire length is invalid.");
				return false;
			}
			const FUTF8ToTCHAR Converted(
				reinterpret_cast<const ANSICHAR*>(
					Bytes.GetData() + Cursor),
				Utf8Length);
			const FString TagText(
				Converted.Length(), Converted.Get());
			Cursor += static_cast<int32>(Utf8Length);
			const FTCHARToUTF8 RoundTrip(*TagText, TagText.Len());
			if (TagText.Len() != FCString::Strlen(*TagText)
				|| RoundTrip.Length()
					!= static_cast<int32>(Utf8Length)
				|| FMemory::Memcmp(
					RoundTrip.Get(),
					Bytes.GetData() + Cursor
						- static_cast<int32>(Utf8Length),
					Utf8Length) != 0)
			{
				OutError =
					TEXT("Ability resource tag is not canonical UTF-8.");
				return false;
			}
			int64 Amount = 0;
			const FName ExistingName(*TagText, FNAME_Find);
			const FGameplayTag Tag = ExistingName.IsNone()
				? FGameplayTag()
				: UGameplayTagsManager::Get().RequestGameplayTag(
					ExistingName, false);
			if (!ReadInt64(Bytes, Cursor, Amount)
				|| !Tag.IsValid()
				|| (!PreviousTag.IsEmpty()
					&& TagText <= PreviousTag))
			{
				OutError =
					TEXT("Ability resource-cost tags are unknown, duplicated, or non-canonical.");
				return false;
			}
			PreviousTag = TagText;
			OutCost.Amounts.Add(Tag, FFixedPoint(Amount));
		}
		return true;
	}

	struct FProviderClaim
	{
		uint64 Token = 0;
		FString Owner;
		FSeinPoolObjectCodecDescriptor Descriptor;
		FGuid DescriptorDigest;
		FString CanonicalDescriptor;
		FSeinPoolObjectCodecOps Ops;
		TStrongObjectPtr<UClass> AnchorRoot;
	};

	struct FProviderEntry
	{
		FString AnchorPath;
		ESeinPoolObjectKind Kind = ESeinPoolObjectKind::Ability;
		FString Owner;
		FString StableProviderId;
		TArray<FProviderClaim> Claims;
	};

	TArray<FProviderEntry>& Registry()
	{
		static TArray<FProviderEntry> Value;
		return Value;
	}

	uint64& NextToken()
	{
		static uint64 Value = 1;
		return Value;
	}

#if WITH_DEV_AUTOMATION_TESTS
	struct FTestLocalClassAdmission
	{
		uint64 Token = 0;
		TStrongObjectPtr<UClass> ClassRoot;
	};

	TArray<FTestLocalClassAdmission>& TestLocalClassAdmissions()
	{
		static TArray<FTestLocalClassAdmission> Value;
		return Value;
	}
#endif

	uint64 AllocateToken()
	{
		uint64& Candidate = NextToken();
		if (Candidate == 0)
		{
			Candidate = 1;
		}
		return Candidate++;
	}

	void SetError(FString* OutError, FString Error)
	{
		if (OutError)
		{
			*OutError = MoveTemp(Error);
		}
	}

	void AppendFramed(FString& Out, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Out += FString::Printf(TEXT("%d:"), Utf8.Length());
		Out += Value;
		Out += TEXT("\n");
	}

	const UClass* BaseClassForKind(ESeinPoolObjectKind Kind)
	{
		switch (Kind)
		{
		case ESeinPoolObjectKind::Ability:
			return USeinAbility::StaticClass();
		case ESeinPoolObjectKind::CommandBrokerResolver:
			return USeinCommandBrokerResolver::StaticClass();
		default:
			return nullptr;
		}
	}

	bool IsConcreteClass(const UClass* Class)
	{
		return Class
			&& !Class->HasAnyClassFlags(
				CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
	}

	const UClass* FindNearestNativeAnchor(const UClass* Class)
	{
		for (const UClass* Cursor = Class; Cursor;
			Cursor = Cursor->GetSuperClass())
		{
			if (!Cursor->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
			{
				return Cursor;
			}
		}
		return nullptr;
	}

	bool ShouldSkipReflectedProperty(const FProperty& Property)
	{
		if (FSeinCanonicalStatePropertyPolicy::ShouldSkip(
				Property))
		{
			return true;
		}

		// Native EditDefaultsOnly fields are immutable class configuration, not
		// per-instance continuation state. Their values are already covered by the
		// frozen simulation-content digest, and a materialized object receives the
		// same values from its exact-class CDO before runtime state is decoded.
		// Keeping them in every pool record duplicated large tag/config payloads
		// hundreds of times. Do not apply this rule to Blueprint-declared variables:
		// designers commonly leave runtime BP state non-instance-editable, so those
		// fields must remain in the reflected pool contract.
		const UClass* OwnerClass = Cast<UClass>(Property.GetOwnerStruct());
		if (OwnerClass
			&& !OwnerClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
			&& Property.HasAllPropertyFlags(
				CPF_Edit | CPF_DisableEditOnInstance))
		{
			return true;
		}

		const UStruct* Owner = Property.GetOwnerStruct();
		const FName Name = Property.GetFName();
		if (Owner == USeinAbility::StaticClass())
		{
			return Name
					== GET_MEMBER_NAME_CHECKED(
						USeinAbility, ResourceCost)
				|| Name
					== GET_MEMBER_NAME_CHECKED(
						USeinAbility, ValidTargetTags)
				|| Name
					== GET_MEMBER_NAME_CHECKED(
						USeinAbility, TargeterSpec)
				|| Name
					== GET_MEMBER_NAME_CHECKED(
						USeinAbility, DeductedCost)
				|| Name
					== GET_MEMBER_NAME_CHECKED(
						USeinAbility,
						PendingCompletionCost);
		}
		if (Owner == USeinDefaultCommandBrokerResolver::StaticClass())
		{
			return Name
					== GET_MEMBER_NAME_CHECKED(
						USeinDefaultCommandBrokerResolver,
						DefaultFormationClass)
				|| Name
					== GET_MEMBER_NAME_CHECKED(
						USeinDefaultCommandBrokerResolver,
						FormationsByTag);
		}
		return false;
	}

	bool ShouldSerializeReflectedProperty(const FProperty& Property)
	{
		return !ShouldSkipReflectedProperty(Property);
	}

	bool CaptureReflectedPoolObjectState(
		const UObject& Object,
		TArray<uint8>& OutBytes,
		FString& OutError)
	{
		OutBytes.Reset();
		OutError.Reset();
		TArray<uint8> ReflectedBytes;
		if (!FSeinCanonicalStateCodec::EncodeObject(
				Object,
				{},
				MakeReflectedWireLimits(),
				&ShouldSerializeReflectedProperty,
				ReflectedBytes,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Reflected pool state failed bounded serialization.");
			}
			OutBytes.Reset();
			return false;
		}
		const USeinAbility* Ability = Cast<USeinAbility>(&Object);
		AppendUInt32(OutBytes, 0x53504f52); // "SPOR"
		AppendUInt32(OutBytes, ReflectedEnvelopeVersion);
		AppendUInt32(OutBytes, Ability ? 1u : 0u);
		AppendUInt32(
			OutBytes,
			static_cast<uint32>(ReflectedBytes.Num()));
		OutBytes.Append(ReflectedBytes);
		if (Ability
			&& (!AppendResourceCost(
					Ability->DeductedCost,
					OutBytes,
					OutError)
				|| !AppendResourceCost(
					Ability->PendingCompletionCost,
					OutBytes,
					OutError)))
		{
			OutBytes.Reset();
			return false;
		}
		if (OutBytes.Num()
			> FSeinPoolObjectCodecRegistry::MaxStateBytes)
		{
			OutError =
				TEXT("Reflected pool envelope exceeds its byte bound.");
			OutBytes.Reset();
			return false;
		}
		return true;
	}

	bool StructMayCopyVariableDefaults(
		const UScriptStruct& Struct,
		TSet<const UScriptStruct*>& Visiting);

	bool PropertyMayInitializeVariableDefaults(
		const FProperty& Property,
		TSet<const UScriptStruct*>& Visiting)
	{
		if (Property.IsA<FStrProperty>()
			|| Property.IsA<FTextProperty>()
			|| Property.IsA<FArrayProperty>()
			|| Property.IsA<FMapProperty>()
			|| Property.IsA<FSetProperty>())
		{
			return true;
		}
		if (const FOptionalProperty* Optional =
			CastField<FOptionalProperty>(&Property))
		{
			return PropertyMayInitializeVariableDefaults(
				*Optional->GetValueProperty(), Visiting);
		}
		if (const FStructProperty* StructProperty =
			CastField<FStructProperty>(&Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (!Struct)
			{
				return true;
			}
			if (Struct == FGameplayTag::StaticStruct())
			{
				return false;
			}
			if (Struct == FGameplayTagContainer::StaticStruct()
				|| Struct == FGameplayTagQuery::StaticStruct())
			{
				return true;
			}
			return StructMayCopyVariableDefaults(
				*Struct, Visiting);
		}
		return false;
	}

	bool StructMayCopyVariableDefaults(
		const UScriptStruct& Struct,
		TSet<const UScriptStruct*>& Visiting)
	{
		if (Visiting.Contains(&Struct))
		{
			return false;
		}
		if (const UScriptStruct::ICppStructOps* StructOps =
				Struct.GetCppStructOps();
			StructOps
				&& !StructOps->HasZeroConstructor()
				&& (StructOps->HasDestructor()
					|| StructOps->HasPostScriptConstruct()))
		{
			// A non-zero native constructor with teardown or post-construct
			// behavior can own hidden, unreflected variable storage. Reflected
			// bounds cannot account for it. Blueprint/UDS structs have no such
			// native constructor and continue through the recursive field audit.
			return true;
		}
		Visiting.Add(&Struct);
		for (TFieldIterator<FProperty> It(
			&Struct, EFieldIteratorFlags::IncludeSuper);
			It; ++It)
		{
			if (PropertyMayInitializeVariableDefaults(
				**It, Visiting))
			{
				Visiting.Remove(&Struct);
				return true;
			}
		}
		Visiting.Remove(&Struct);
		return false;
	}

	bool MayInitializeVariableDefaults(
		const FProperty& Property)
	{
		if (const FStructProperty* StructProperty =
			CastField<FStructProperty>(&Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (!Struct
				|| Struct == FGameplayTag::StaticStruct()
				|| Struct == FGameplayTagContainer::StaticStruct()
				|| Struct == FGameplayTagQuery::StaticStruct())
			{
				return false;
			}
			TSet<const UScriptStruct*> Visiting;
			return StructMayCopyVariableDefaults(
				*Struct, Visiting);
		}
		if (const FOptionalProperty* Optional =
			CastField<FOptionalProperty>(&Property))
		{
			return MayInitializeVariableDefaults(
				*Optional->GetValueProperty());
		}
		return false;
	}

	bool ValidateNestedReflectedProperty(
		const FProperty& Property,
		TSet<const UScriptStruct*>& Visited,
		FString& OutError)
	{
		if (ShouldSkipReflectedProperty(Property))
		{
			return true;
		}
		if (const FStructProperty* StructProperty =
			CastField<FStructProperty>(&Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (!Struct)
			{
				OutError = TEXT("A reflected pool field has no struct type.");
				return false;
			}
			if (Struct == FGameplayTag::StaticStruct()
				|| Struct == FGameplayTagContainer::StaticStruct())
			{
				return true;
			}
			if (Struct == FGameplayTagQuery::StaticStruct())
			{
				OutError = FString::Printf(
					TEXT("Reflected pool field '%s' contains a gameplay-tag query whose token grammar is not accepted from snapshot bytes; keep immutable queries on the locally fingerprinted CDO or register an explicit bounded codec."),
					*Property.GetPathName());
				return false;
			}
			const bool bAllowedNativeSerializer =
				Struct == FFixedPoint::StaticStruct();
			if ((Struct->StructFlags & STRUCT_PostSerializeNative)
				|| ((Struct->StructFlags & STRUCT_SerializeNative)
					&& !bAllowedNativeSerializer))
			{
				OutError = FString::Printf(
					TEXT("Blueprint pool field '%s' contains native serializer behavior in struct '%s'; mark immutable data Transient or register an explicit codec for its nearest native anchor."),
					*Property.GetPathName(),
					*Struct->GetPathName());
				return false;
			}
			if (!Visited.Contains(Struct))
			{
				Visited.Add(Struct);
				for (TFieldIterator<FProperty> It(
					Struct, EFieldIteratorFlags::IncludeSuper);
					It; ++It)
				{
					if (!ValidateNestedReflectedProperty(
						**It, Visited, OutError))
					{
						return false;
					}
				}
			}
			return true;
		}
		if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
		{
			if (MayInitializeVariableDefaults(*Array->Inner))
			{
				OutError = FString::Printf(
					TEXT("Reflected pool array '%s' has struct elements whose authored defaults can allocate variable storage before decode bounds apply; use fixed-layout elements or an explicit codec."),
					*Property.GetPathName());
				return false;
			}
			return ValidateNestedReflectedProperty(
				*Array->Inner, Visited, OutError);
		}
		if (Property.IsA<FSetProperty>())
		{
			OutError = FString::Printf(
				TEXT("Reflected pool field '%s' is a TSet; use an ordered array or an explicit canonical codec."),
				*Property.GetPathName());
			return false;
		}
		if (Property.IsA<FMapProperty>())
		{
			OutError = FString::Printf(
				TEXT("Reflected pool field '%s' is a TMap; use a canonically ordered array or an explicit codec."),
				*Property.GetPathName());
			return false;
		}
		if (const FOptionalProperty* Optional =
			CastField<FOptionalProperty>(&Property))
		{
			if (MayInitializeVariableDefaults(
					*Optional->GetValueProperty()))
			{
				OutError = FString::Printf(
					TEXT("Reflected pool optional '%s' has a struct value whose authored default can allocate variable storage before decode bounds apply; use a fixed-layout value or an explicit codec."),
					*Property.GetPathName());
				return false;
			}
			return ValidateNestedReflectedProperty(
				*Optional->GetValueProperty(), Visited, OutError);
		}
		if (const FEnumProperty* Enum =
			CastField<FEnumProperty>(&Property))
		{
			return ValidateNestedReflectedProperty(
				*Enum->GetUnderlyingProperty(), Visited, OutError);
		}
		if (const FNumericProperty* Numeric =
			CastField<FNumericProperty>(&Property))
		{
			if (!Numeric->IsFloatingPoint())
			{
				return true;
			}
			OutError = FString::Printf(
				TEXT("Reflected pool field '%s' is floating point."),
				*Property.GetPathName());
			return false;
		}
		if (Property.IsA<FBoolProperty>()
			|| Property.IsA<FNameProperty>()
			|| Property.IsA<FStrProperty>())
		{
			return true;
		}
		OutError = FString::Printf(
			TEXT("Reflected pool field '%s' uses unsupported reference, delegate, text, or opaque property type '%s'; mark non-state data Transient or register an explicit codec."),
			*Property.GetPathName(),
			*Property.GetClass()->GetName());
		return false;
	}

	bool ValidateReflectedClassSchemaInternal(
		const UClass& ExactClass,
		FString& OutError)
	{
		OutError.Reset();
		for (TFieldIterator<FProperty> It(
			&ExactClass, EFieldIteratorFlags::IncludeSuper);
			It; ++It)
		{
			if (ShouldSkipReflectedProperty(**It))
			{
				continue;
			}
			TSet<const UScriptStruct*> Visited;
			if (!ValidateNestedReflectedProperty(**It, Visited, OutError))
			{
				return false;
			}
		}
		if (!FSeinCanonicalStateCodec::ValidateObjectClass(
				&ExactClass,
				&ShouldSerializeReflectedProperty,
				OutError))
		{
			return false;
		}
		return true;
	}

	bool CanonicalizeDescriptor(
		FName OwnerModuleId,
		const FSeinPoolObjectCodecDescriptor& Descriptor,
		FSeinPoolObjectCodecDescriptor& OutDescriptor,
		FString& OutOwner,
		FGuid& OutDigest,
		FString& OutCanonical,
		FString& OutError)
	{
		if (!FSeinSimulationContentManifestCodec::CanonicalizeStableId(
				OwnerModuleId.ToString(), OutOwner, OutError)
			|| !FSeinSimulationContentManifestCodec::CanonicalizeStableId(
				Descriptor.StableProviderId,
				OutDescriptor.StableProviderId,
				OutError))
		{
			return false;
		}
		OutDescriptor.NativeAnchor = Descriptor.NativeAnchor;
		OutDescriptor.Kind = Descriptor.Kind;
		OutDescriptor.StateSchemaVersion =
			Descriptor.StateSchemaVersion;
		OutDescriptor.BehaviorRevision =
			Descriptor.BehaviorRevision;
		OutDescriptor.CodecRevision = Descriptor.CodecRevision;
		OutDescriptor.MaxStateBytes = Descriptor.MaxStateBytes;
		OutDescriptor.bAllowBlueprintChildren =
			Descriptor.bAllowBlueprintChildren;
		OutDescriptor.bUsesReflectedState =
			Descriptor.bUsesReflectedState;

		const UClass* ExpectedBase =
			BaseClassForKind(OutDescriptor.Kind);
		if (!ExpectedBase
			|| !OutDescriptor.NativeAnchor
			|| !OutDescriptor.NativeAnchor->IsChildOf(ExpectedBase)
			|| OutDescriptor.NativeAnchor->HasAnyClassFlags(
				CLASS_CompiledFromBlueprint
					| CLASS_Deprecated
					| CLASS_NewerVersionExists)
			|| OutDescriptor.StateSchemaVersion == 0
			|| OutDescriptor.BehaviorRevision == 0
			|| OutDescriptor.CodecRevision == 0
			|| OutDescriptor.MaxStateBytes <= 0
			|| OutDescriptor.MaxStateBytes
				> FSeinPoolObjectCodecRegistry::MaxStateBytes)
		{
			OutError =
				TEXT("Pool codecs require a native ability/resolver anchor, positive revisions, and a bounded state size.");
			return false;
		}

		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.PoolObject.CodecDescriptor"),
			DescriptorFormatVersion);
		const FString AnchorPath =
			OutDescriptor.NativeAnchor->GetPathName();
		const bool bOK =
			Writer.WriteUInt32(
				static_cast<uint32>(OutDescriptor.Kind))
			&& Writer.WriteString(AnchorPath)
			&& Writer.WriteString(OutDescriptor.StableProviderId)
			&& Writer.WriteUInt32(OutDescriptor.StateSchemaVersion)
			&& Writer.WriteUInt32(OutDescriptor.BehaviorRevision)
			&& Writer.WriteUInt32(OutDescriptor.CodecRevision)
			&& Writer.WriteInt32(OutDescriptor.MaxStateBytes)
			&& Writer.WriteBool(
				OutDescriptor.bAllowBlueprintChildren)
			&& Writer.WriteBool(
				OutDescriptor.bUsesReflectedState);

		OutCanonical =
			TEXT("SeinARTS.PoolObject.CodecDescriptor\n1\n");
		AppendFramed(
			OutCanonical,
			LexToString(static_cast<uint8>(OutDescriptor.Kind)));
		AppendFramed(OutCanonical, AnchorPath);
		AppendFramed(
			OutCanonical, OutDescriptor.StableProviderId);
		AppendFramed(
			OutCanonical,
			LexToString(OutDescriptor.StateSchemaVersion));
		AppendFramed(
			OutCanonical,
			LexToString(OutDescriptor.BehaviorRevision));
		AppendFramed(
			OutCanonical,
			LexToString(OutDescriptor.CodecRevision));
		AppendFramed(
			OutCanonical,
			LexToString(OutDescriptor.MaxStateBytes));
		AppendFramed(
			OutCanonical,
			OutDescriptor.bAllowBlueprintChildren
				? TEXT("1") : TEXT("0"));
		AppendFramed(
			OutCanonical,
			OutDescriptor.bUsesReflectedState
				? TEXT("1") : TEXT("0"));
		return bOK && Writer.Finalize(OutDigest, OutError);
	}

	bool ResolveClaim(
		uint64 Token,
		FProviderClaim& OutClaim,
		FString& OutError)
	{
		OutClaim = {};
		for (const FProviderEntry& Entry : Registry())
		{
			if (const FProviderClaim* Claim =
				Entry.Claims.FindByPredicate(
					[Token](const FProviderClaim& Candidate)
					{
						return Candidate.Token == Token;
					}))
			{
				OutClaim.Token = Claim->Token;
				OutClaim.Owner = Claim->Owner;
				OutClaim.Descriptor = Claim->Descriptor;
				OutClaim.DescriptorDigest =
					Claim->DescriptorDigest;
				OutClaim.CanonicalDescriptor =
					Claim->CanonicalDescriptor;
				OutClaim.Ops = Claim->Ops;
				OutClaim.AnchorRoot.Reset(
					const_cast<UClass*>(
						Claim->Descriptor.NativeAnchor));
				return true;
			}
		}
		OutError = FString::Printf(
			TEXT("Pool codec generation %llu is no longer registered."),
			static_cast<unsigned long long>(Token));
		return false;
	}
}

struct FSeinPoolObjectCodecManifest::FData
{
	struct FProvider
	{
		uint64 Token = 0;
		FString Owner;
		FSeinPoolObjectCodecDescriptor Descriptor;
		FGuid DescriptorDigest;
		FString CanonicalDescriptor;
		FSeinPoolObjectCodecOps Ops;
		TStrongObjectPtr<UClass> AnchorRoot;
	};

	struct FAdmittedClass
	{
		FString ExactClassPath;
		ESeinPoolObjectKind Kind = ESeinPoolObjectKind::Ability;
		int32 ProviderIndex = INDEX_NONE;
		FGuid ClassSchemaDigest;
		FGuid RootClassContractDigest;
		uint64 TestAdmissionToken = 0;
		TStrongObjectPtr<UClass> ClassRoot;
	};

	TArray<FProvider> Providers;
	TArray<FAdmittedClass> Classes;
	TMap<const UClass*, int32> ClassIndexByPointer;
	FString CanonicalManifest;
	FGuid Digest;
};

namespace
{
	const FSeinPoolObjectCodecManifest::FData::FAdmittedClass*
	FindAdmittedClass(
		const FSeinPoolObjectCodecManifest::FData* Data,
		const FString& ExactClassPath,
		ESeinPoolObjectKind Kind)
	{
		if (!Data)
		{
			return nullptr;
		}
		return Data->Classes.FindByPredicate(
			[&ExactClassPath, Kind](
				const FSeinPoolObjectCodecManifest::FData::
					FAdmittedClass& Candidate)
			{
				return Candidate.Kind == Kind
					&& Candidate.ExactClassPath == ExactClassPath;
			});
	}

	const FSeinPoolObjectCodecManifest::FData::FAdmittedClass*
	FindAdmittedClass(
		const FSeinPoolObjectCodecManifest::FData* Data,
		const UClass* ExactClass,
		ESeinPoolObjectKind Kind)
	{
		if (!Data || !ExactClass)
		{
			return nullptr;
		}
		const int32* Index =
			Data->ClassIndexByPointer.Find(ExactClass);
		return Index
			&& Data->Classes.IsValidIndex(*Index)
			&& Data->Classes[*Index].Kind == Kind
				? &Data->Classes[*Index]
				: nullptr;
	}

	bool AddAdmittedClass(
		FSeinPoolObjectCodecManifest::FData& Data,
		UClass& ExactClass,
		ESeinPoolObjectKind Kind,
		int32 ProviderIndex,
		uint64 TestAdmissionToken,
		FString& OutError)
	{
		if (!Data.Providers.IsValidIndex(ProviderIndex)
			|| !IsConcreteClass(&ExactClass)
			|| ExactClass.GetPathName().Len() > 1024
			|| !ExactClass.IsChildOf(BaseClassForKind(Kind)))
		{
			OutError = FString::Printf(
				TEXT("Invalid locally admitted pool class '%s'."),
				*ExactClass.GetPathName());
			return false;
		}
		const FSeinPoolObjectCodecManifest::FData::FProvider&
			Provider = Data.Providers[ProviderIndex];
		if (Provider.Descriptor.Kind != Kind)
		{
			OutError =
				TEXT("A pool class resolved to the wrong provider kind.");
			return false;
		}
		if (ExactClass.HasAnyClassFlags(CLASS_CompiledFromBlueprint))
		{
			if (!Provider.Descriptor.bAllowBlueprintChildren
				|| FindNearestNativeAnchor(&ExactClass)
					!= Provider.Descriptor.NativeAnchor
				|| (Provider.Descriptor.bUsesReflectedState
					&& !ValidateReflectedClassSchemaInternal(
						ExactClass, OutError)))
			{
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Blueprint pool class '%s' is not admitted by its nearest native anchor."),
						*ExactClass.GetPathName());
				}
				return false;
			}
		}
		else if (Provider.Descriptor.bUsesReflectedState
			&& !ValidateReflectedClassSchemaInternal(
				ExactClass, OutError))
		{
			return false;
		}
		else if (&ExactClass != Provider.Descriptor.NativeAnchor)
		{
			OutError = FString::Printf(
				TEXT("Native pool class '%s' requires its own exact provider registration."),
				*ExactClass.GetPathName());
			return false;
		}

		const FString Path = ExactClass.GetPathName();
		if (const FSeinPoolObjectCodecManifest::FData::FAdmittedClass*
			Existing = Data.Classes.FindByPredicate(
				[&Path](
					const FSeinPoolObjectCodecManifest::FData::
						FAdmittedClass& Candidate)
				{
					return Candidate.ExactClassPath == Path;
				}))
		{
			if (Existing->Kind != Kind
				|| Existing->ProviderIndex != ProviderIndex)
			{
				OutError = FString::Printf(
					TEXT("Pool class '%s' has conflicting local providers."),
					*Path);
				return false;
			}
			return true;
		}

		FSeinPoolObjectCodecManifest::FData::FAdmittedClass&
			Admitted = Data.Classes.AddDefaulted_GetRef();
		Admitted.ExactClassPath = Path;
		Admitted.Kind = Kind;
		Admitted.ProviderIndex = ProviderIndex;
		Admitted.TestAdmissionToken = TestAdmissionToken;
		if (Provider.Descriptor.bUsesReflectedState)
		{
			if (!FSeinCanonicalStateCodec::
				ComputeObjectSchemaDigest(
					&ExactClass,
					&ShouldSerializeReflectedProperty,
					Admitted.ClassSchemaDigest,
					OutError))
			{
				Data.Classes.Pop();
				return false;
			}
		}
		else
		{
			Admitted.ClassSchemaDigest =
				Provider.DescriptorDigest;
		}
		FSeinCanonicalDigestWriter RootContractWriter(
			TEXT("SeinARTS.PoolObject.RootClassContract"), 1);
		if (!RootContractWriter.WriteUInt8(
				static_cast<uint8>(Kind))
			|| !RootContractWriter.WriteString(Path)
			|| !RootContractWriter.WriteGuid(
				Provider.DescriptorDigest)
			|| !RootContractWriter.WriteGuid(
				Admitted.ClassSchemaDigest)
			|| !RootContractWriter.Finalize(
				Admitted.RootClassContractDigest,
				OutError))
		{
			Data.Classes.Pop();
			return false;
		}
		Admitted.ClassRoot.Reset(&ExactClass);
		return true;
	}
}

FSeinPoolObjectCodecRegistrationHandle::
	~FSeinPoolObjectCodecRegistrationHandle()
{
	Reset();
}

FSeinPoolObjectCodecRegistrationHandle::
	FSeinPoolObjectCodecRegistrationHandle(
	FSeinPoolObjectCodecRegistrationHandle&& Other) noexcept
	: Token(Other.Token)
{
	check(IsInGameThread());
	Other.Token = 0;
}

FSeinPoolObjectCodecRegistrationHandle&
FSeinPoolObjectCodecRegistrationHandle::operator=(
	FSeinPoolObjectCodecRegistrationHandle&& Other) noexcept
{
	check(IsInGameThread());
	if (this != &Other)
	{
		Reset();
		Token = Other.Token;
		Other.Token = 0;
	}
	return *this;
}

void FSeinPoolObjectCodecRegistrationHandle::Reset()
{
	if (Token == 0)
	{
		return;
	}
	check(IsInGameThread());
	if (FSeinStateProviderTransactionScope::IsActive())
	{
		UE_LOG(LogSeinSim, Fatal,
			TEXT("A pool codec registration handle was destroyed inside a state-provider callback transaction."));
	}
	const bool bRemoved =
		FSeinPoolObjectCodecRegistry::UnregisterToken(Token);
	check(bRemoved);
	Token = 0;
}

#if WITH_DEV_AUTOMATION_TESTS
FSeinPoolObjectLocalClassAdmissionHandle::
	~FSeinPoolObjectLocalClassAdmissionHandle()
{
	Reset();
}

FSeinPoolObjectLocalClassAdmissionHandle::
	FSeinPoolObjectLocalClassAdmissionHandle(
		FSeinPoolObjectLocalClassAdmissionHandle&& Other) noexcept
	: Token(Other.Token)
{
	check(IsInGameThread());
	Other.Token = 0;
}

FSeinPoolObjectLocalClassAdmissionHandle&
FSeinPoolObjectLocalClassAdmissionHandle::operator=(
	FSeinPoolObjectLocalClassAdmissionHandle&& Other) noexcept
{
	check(IsInGameThread());
	if (this != &Other)
	{
		Reset();
		Token = Other.Token;
		Other.Token = 0;
	}
	return *this;
}

void FSeinPoolObjectLocalClassAdmissionHandle::Reset()
{
	if (Token != 0)
	{
		check(IsInGameThread());
		const bool bRemoved =
			FSeinPoolObjectCodecRegistry::
				UnregisterTestLocalClass(Token);
		check(bRemoved);
		Token = 0;
	}
}
#endif

int32 FSeinPoolObjectCodecManifest::NumProviders() const
{
	return Data.IsValid() ? Data->Providers.Num() : 0;
}

int32 FSeinPoolObjectCodecManifest::NumAdmittedClasses() const
{
	return Data.IsValid() ? Data->Classes.Num() : 0;
}

const FString&
FSeinPoolObjectCodecManifest::GetCanonicalManifest() const
{
	static const FString Empty;
	return Data.IsValid() ? Data->CanonicalManifest : Empty;
}

FGuid FSeinPoolObjectCodecManifest::GetDigest() const
{
	return Data.IsValid() ? Data->Digest : FGuid();
}

bool FSeinPoolObjectCodecManifest::VerifyProviderLeases(
	FString& OutError) const
{
	check(IsInGameThread());
	OutError.Reset();
	if (!Data.IsValid()
		|| FSeinStateProviderTransactionScope::IsActive())
	{
		OutError =
			TEXT("Pool provider leases require a valid manifest outside provider callbacks.");
		return false;
	}
	FSeinStateProviderTransactionScope Transaction;
	for (const FData::FProvider& Provider : Data->Providers)
	{
		FProviderClaim Claim;
		if (!ResolveClaim(Provider.Token, Claim, OutError)
			|| Claim.DescriptorDigest
				!= Provider.DescriptorDigest)
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("A frozen pool provider generation changed.");
			}
			return false;
		}
	}
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FSeinPoolObjectCodecManifest::IsClassAdmittedForTests(
	const UClass* ExactClass,
	ESeinPoolObjectKind Kind) const
{
	return ExactClass
		&& FindAdmittedClass(Data.Get(), ExactClass->GetPathName(), Kind);
}
#endif

FSeinPoolObjectCodecRegistrationHandle
FSeinPoolObjectCodecRegistry::Register(
	FName OwnerModuleId,
	const FSeinPoolObjectCodecDescriptor& Descriptor,
	FSeinPoolObjectCodecOps Ops,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (FSeinStateProviderTransactionScope::IsActive())
	{
		SetError(
			OutError,
			TEXT("Pool providers cannot register inside another state-provider callback."));
		return {};
	}

	FSeinPoolObjectCodecDescriptor CanonicalDescriptor;
	FString Owner;
	FGuid DescriptorDigest;
	FString CanonicalDescriptorText;
	FString Error;
	if (!CanonicalizeDescriptor(
			OwnerModuleId,
			Descriptor,
			CanonicalDescriptor,
			Owner,
			DescriptorDigest,
			CanonicalDescriptorText,
			Error)
		|| (CanonicalDescriptor.bUsesReflectedState
			&& !ValidateReflectedClassSchemaInternal(
				*CanonicalDescriptor.NativeAnchor,
				Error))
		|| !Ops.Capture || !Ops.Materialize)
	{
		if (Error.IsEmpty())
		{
			Error =
				TEXT("Pool providers require capture and materialize callbacks.");
		}
		SetError(OutError, Error);
		return {};
	}

	const FString AnchorPath =
		CanonicalDescriptor.NativeAnchor->GetPathName();
	for (const FProviderEntry& Existing : Registry())
	{
		if (Existing.AnchorPath != AnchorPath
			&& Existing.StableProviderId
				== CanonicalDescriptor.StableProviderId)
		{
			Error = FString::Printf(
				TEXT("Pool provider ID '%s' is already bound to another native anchor."),
				*CanonicalDescriptor.StableProviderId);
			SetError(OutError, Error);
			return {};
		}
	}

	FProviderEntry* Entry = Registry().FindByPredicate(
		[&AnchorPath, &CanonicalDescriptor](
			const FProviderEntry& Candidate)
		{
			return Candidate.AnchorPath == AnchorPath
				&& Candidate.Kind == CanonicalDescriptor.Kind;
		});
	if (Entry)
	{
		if (Entry->Owner != Owner
			|| Entry->StableProviderId
				!= CanonicalDescriptor.StableProviderId
			|| Entry->Claims.IsEmpty()
			|| Entry->Claims[0].DescriptorDigest
				!= DescriptorDigest
			|| Entry->Claims.Num() >= MaxReloadClaimsPerAnchor)
		{
			Error = FString::Printf(
				TEXT("Conflicting or saturated pool provider claim for '%s'."),
				*AnchorPath);
			SetError(OutError, Error);
			return {};
		}
	}
	else
	{
		Entry = &Registry().AddDefaulted_GetRef();
		Entry->AnchorPath = AnchorPath;
		Entry->Kind = CanonicalDescriptor.Kind;
		Entry->Owner = Owner;
		Entry->StableProviderId =
			CanonicalDescriptor.StableProviderId;
	}

	FProviderClaim Claim;
	Claim.Token = AllocateToken();
	Claim.Owner = Owner;
	Claim.Descriptor = MoveTemp(CanonicalDescriptor);
	Claim.DescriptorDigest = DescriptorDigest;
	Claim.CanonicalDescriptor = MoveTemp(CanonicalDescriptorText);
	Claim.Ops = MoveTemp(Ops);
	Claim.AnchorRoot.Reset(
		const_cast<UClass*>(Claim.Descriptor.NativeAnchor));
	const uint64 Token = Claim.Token;
	Entry->Claims.Add(MoveTemp(Claim));
	return FSeinPoolObjectCodecRegistrationHandle(Token);
}

bool FSeinPoolObjectCodecRegistry::Unregister(
	FSeinPoolObjectCodecRegistrationHandle& Handle)
{
	check(IsInGameThread());
	if (!Handle.IsValid()
		|| FSeinStateProviderTransactionScope::IsActive()
		|| !UnregisterToken(Handle.Token))
	{
		return false;
	}
	Handle.Token = 0;
	return true;
}

bool FSeinPoolObjectCodecRegistry::UnregisterToken(uint64 Token)
{
	check(IsInGameThread());
	if (Token == 0
		|| FSeinStateProviderTransactionScope::IsActive())
	{
		return false;
	}
	for (int32 EntryIndex = 0;
		EntryIndex < Registry().Num();
		++EntryIndex)
	{
		FProviderEntry& Entry = Registry()[EntryIndex];
		const int32 ClaimIndex =
			Entry.Claims.IndexOfByPredicate(
				[Token](const FProviderClaim& Claim)
				{
					return Claim.Token == Token;
				});
		if (ClaimIndex == INDEX_NONE)
		{
			continue;
		}

		const FString Owner = Entry.Claims[ClaimIndex].Owner;
		for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
		{
			if (It->HasAnyFlags(RF_ClassDefaultObject)
				|| !It->PoolObjectCodecManifest.Data.IsValid())
			{
				continue;
			}
			const bool bUsesToken =
				It->PoolObjectCodecManifest.Data->Providers
					.ContainsByPredicate(
						[Token](
							const FSeinPoolObjectCodecManifest::
								FData::FProvider& Provider)
						{
							return Provider.Token == Token;
						});
			if (bUsesToken)
			{
				It->TerminateAndReleaseForModuleUnload(
					FName(*Owner),
					TEXT("its frozen pool-object codec generation unloaded"));
			}
		}

		FProviderClaim Removed =
			MoveTemp(Entry.Claims[ClaimIndex]);
		Entry.Claims.RemoveAt(ClaimIndex);
		if (Entry.Claims.IsEmpty())
		{
			Registry().RemoveAt(EntryIndex);
		}
		{
			FSeinStateProviderTransactionScope Transaction;
			Removed = {};
		}
		return true;
	}
	return false;
}

FSeinPoolObjectCodecManifest
FSeinPoolObjectCodecRegistry::CaptureManifest(
	const FSeinSimulationContentManifestProfile& ContentProfile,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (FSeinStateProviderTransactionScope::IsActive()
		|| !ContentProfile.RootDigest.IsValid())
	{
		SetError(
			OutError,
			TEXT("Pool manifest capture requires a validated local content profile outside provider callbacks."));
		return {};
	}

	TSharedRef<FSeinPoolObjectCodecManifest::FData,
		ESPMode::ThreadSafe> Data =
		MakeShared<
			FSeinPoolObjectCodecManifest::FData,
			ESPMode::ThreadSafe>();
	for (const FProviderEntry& Registered : Registry())
	{
		if (Registered.Claims.IsEmpty())
		{
			continue;
		}
		const FProviderClaim& Claim = Registered.Claims.Last();
		FSeinPoolObjectCodecManifest::FData::FProvider&
			Frozen = Data->Providers.AddDefaulted_GetRef();
		Frozen.Token = Claim.Token;
		Frozen.Owner = Claim.Owner;
		Frozen.Descriptor = Claim.Descriptor;
		Frozen.DescriptorDigest = Claim.DescriptorDigest;
		Frozen.CanonicalDescriptor = Claim.CanonicalDescriptor;
		Frozen.Ops = Claim.Ops;
		Frozen.AnchorRoot.Reset(
			const_cast<UClass*>(
				Claim.Descriptor.NativeAnchor));
	}
	Data->Providers.Sort(
		[](const FSeinPoolObjectCodecManifest::FData::FProvider& A,
			const FSeinPoolObjectCodecManifest::FData::FProvider& B)
		{
			if (A.Descriptor.StableProviderId
				!= B.Descriptor.StableProviderId)
			{
				return A.Descriptor.StableProviderId
					< B.Descriptor.StableProviderId;
			}
			return A.Descriptor.NativeAnchor->GetPathName()
				< B.Descriptor.NativeAnchor->GetPathName();
		});

	TMap<FString, int32> BlueprintProviderByNativeAnchor;
	FString Error;
	for (int32 Index = 0; Index < Data->Providers.Num(); ++Index)
	{
		const FSeinPoolObjectCodecManifest::FData::FProvider&
			Provider = Data->Providers[Index];
		UClass* Anchor =
			const_cast<UClass*>(Provider.Descriptor.NativeAnchor);
		if (IsConcreteClass(Anchor)
			&& !AddAdmittedClass(
				*Data,
				*Anchor,
				Provider.Descriptor.Kind,
				Index,
				0,
				Error))
		{
			SetError(OutError, MoveTemp(Error));
			return {};
		}
		if (Provider.Descriptor.bAllowBlueprintChildren)
		{
			BlueprintProviderByNativeAnchor.Add(
				Anchor->GetPathName(), Index);
		}
	}

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")).Get();
	// Standalone/cooked worlds can freeze their lockstep contracts while the
	// registry's initial disk scan is still running. Querying listed packages
	// before that scan completes silently admits only native anchors, even when
	// the validated content profile explicitly contains Blueprint abilities and
	// resolvers. Contract freeze is a one-time pre-match boundary, so finish the
	// scan here rather than producing a manifest that cannot checkpoint the live
	// objects the same profile is allowed to spawn.
	AssetRegistry.WaitForCompletion();
	TSet<FString> SynchronouslyScannedPackagePaths;
	TSet<FString> SeenGeneratedClassPaths;
	for (const FSeinSimulationContentRecord& Record :
		ContentProfile.Records)
	{
		if (Record.StableRecordKindId
				!= FSeinSimulationContentManifestCodec::
					GetCurrentRecordKindId()
			|| Record.CanonicalRecordId.IsEmpty())
		{
			continue;
		}
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPackageName(
			FName(*Record.CanonicalRecordId),
			Assets,
			true,
			false);
		if (Assets.IsEmpty())
		{
			// Editor -game starts before plugin content is necessarily present in
			// the registry's searched roots. The content profile itself is the
			// bounded allow-list, so synchronously scan only the missing record's
			// containing package path and retry. One directory is scanned at most
			// once even when it contains many admitted classes.
			const FString PackagePath =
				FPackageName::GetLongPackagePath(
					Record.CanonicalRecordId);
			if (!PackagePath.IsEmpty()
				&& !SynchronouslyScannedPackagePaths.Contains(PackagePath))
			{
				SynchronouslyScannedPackagePaths.Add(PackagePath);
				AssetRegistry.ScanPathsSynchronous(
					{ PackagePath },
					/*bForceRescan=*/true,
					/*bIgnoreDenyListScanFilters=*/false);
			}
			AssetRegistry.GetAssetsByPackageName(
				FName(*Record.CanonicalRecordId),
				Assets,
				true,
				false);
		}
		for (const FAssetData& Asset : Assets)
		{
			const FString NativeAnchorExport =
				Asset.GetTagValueRef<FString>(
					FBlueprintTags::NativeParentClassPath);
			const FString NativeAnchorPath =
				FPackageName::ExportTextPathToObjectPath(
					NativeAnchorExport);
			const int32* ProviderIndex =
				BlueprintProviderByNativeAnchor.Find(
					NativeAnchorPath);
			if (!ProviderIndex)
			{
				continue;
			}
			const FString GeneratedExport =
				Asset.GetTagValueRef<FString>(
					FBlueprintTags::GeneratedClassPath);
			const FString GeneratedPath =
				FPackageName::ExportTextPathToObjectPath(
					GeneratedExport);
			if (GeneratedPath.IsEmpty()
				|| SeenGeneratedClassPaths.Contains(GeneratedPath))
			{
				continue;
			}
			SeenGeneratedClassPaths.Add(GeneratedPath);
			UClass* GeneratedClass =
				LoadObject<UClass>(nullptr, *GeneratedPath);
			if (!GeneratedClass
				|| GeneratedClass->GetPathName() != GeneratedPath
				|| !GeneratedClass->HasAnyClassFlags(
					CLASS_CompiledFromBlueprint)
				|| !AddAdmittedClass(
					*Data,
					*GeneratedClass,
					Data->Providers[*ProviderIndex].Descriptor.Kind,
					*ProviderIndex,
					0,
					Error))
			{
				if (Error.IsEmpty())
				{
					Error = FString::Printf(
						TEXT("Locally declared pool Blueprint '%s' could not be loaded or admitted."),
						*GeneratedPath);
				}
				SetError(OutError, MoveTemp(Error));
				return {};
			}
		}
	}

#if WITH_DEV_AUTOMATION_TESTS
	for (const FTestLocalClassAdmission& Admission :
		TestLocalClassAdmissions())
	{
		UClass* ExactClass = Admission.ClassRoot.Get();
		const UClass* NativeAnchor =
			FindNearestNativeAnchor(ExactClass);
		const int32* ProviderIndex = NativeAnchor
			? BlueprintProviderByNativeAnchor.Find(
				NativeAnchor->GetPathName())
			: nullptr;
		if (!ExactClass || !ProviderIndex
			|| !AddAdmittedClass(
				*Data,
				*ExactClass,
				Data->Providers[*ProviderIndex].Descriptor.Kind,
				*ProviderIndex,
				Admission.Token,
				Error))
		{
			if (Error.IsEmpty())
			{
				Error =
					TEXT("An explicit test-local pool class has no compatible native provider.");
			}
			SetError(OutError, MoveTemp(Error));
			return {};
		}
	}
#endif

	Data->Classes.Sort(
		[](const FSeinPoolObjectCodecManifest::FData::FAdmittedClass& A,
			const FSeinPoolObjectCodecManifest::FData::FAdmittedClass& B)
		{
			return A.ExactClassPath < B.ExactClassPath;
		});
	Data->ClassIndexByPointer.Reserve(Data->Classes.Num());
	for (int32 Index = 0; Index < Data->Classes.Num(); ++Index)
	{
		Data->ClassIndexByPointer.Add(
			Data->Classes[Index].ClassRoot.Get(), Index);
	}

	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.PoolObject.CodecManifest"),
		ManifestFormatVersion);
	Data->CanonicalManifest =
		TEXT("SeinARTS.PoolObject.CodecManifest\n1\n");
	bool bOK = Writer.WriteInt32(Data->Providers.Num());
	AppendFramed(
		Data->CanonicalManifest,
		LexToString(Data->Providers.Num()));
	for (const FSeinPoolObjectCodecManifest::FData::FProvider&
		Provider : Data->Providers)
	{
		bOK = bOK
			&& Writer.WriteGuid(Provider.DescriptorDigest);
		AppendFramed(
			Data->CanonicalManifest,
			Provider.CanonicalDescriptor);
	}
	bOK = bOK && Writer.WriteInt32(Data->Classes.Num());
	AppendFramed(
		Data->CanonicalManifest,
		LexToString(Data->Classes.Num()));
	for (const FSeinPoolObjectCodecManifest::FData::FAdmittedClass&
		Class : Data->Classes)
	{
		const FSeinPoolObjectCodecManifest::FData::FProvider&
			Provider = Data->Providers[Class.ProviderIndex];
		bOK = bOK
			&& Writer.WriteUInt32(static_cast<uint32>(Class.Kind))
			&& Writer.WriteString(Class.ExactClassPath)
			&& Writer.WriteGuid(Provider.DescriptorDigest)
			&& Writer.WriteGuid(Class.ClassSchemaDigest);
		AppendFramed(
			Data->CanonicalManifest,
			LexToString(static_cast<uint8>(Class.Kind)));
		AppendFramed(
			Data->CanonicalManifest, Class.ExactClassPath);
		AppendFramed(
			Data->CanonicalManifest,
			Provider.DescriptorDigest.ToString(
				EGuidFormats::Digits));
		AppendFramed(
			Data->CanonicalManifest,
			Class.ClassSchemaDigest.ToString(
				EGuidFormats::Digits));
	}
	if (!bOK || !Writer.Finalize(Data->Digest, Error)
		|| !Data->Digest.IsValid())
	{
		SetError(
			OutError,
			Error.IsEmpty()
				? TEXT("Pool codec manifest digest failed.")
				: MoveTemp(Error));
		return {};
	}

	FSeinPoolObjectCodecManifest Result;
	Result.Data = Data;
	return Result;
}

bool FSeinPoolObjectCodecRegistry::CaptureObject(
	const FSeinPoolObjectCodecManifest& Manifest,
	const UObject& Object,
	ESeinPoolObjectKind ExpectedKind,
	int32 PoolId,
	FSeinSnapshotPoolInstanceRecord& OutRecord,
	FString& OutError)
{
	check(IsInGameThread());
	OutRecord = {};
	OutRecord.PoolID = PoolId;
	OutError.Reset();
	if (!Manifest.IsValid() || PoolId < 0
		|| FSeinStateProviderTransactionScope::IsActive())
	{
		OutError =
			TEXT("Pool capture requires a valid manifest, slot, and callback boundary.");
		return false;
	}
	const FSeinPoolObjectCodecManifest::FData::FAdmittedClass*
		Class = FindAdmittedClass(
			Manifest.Data.Get(),
			Object.GetClass()->GetPathName(),
			ExpectedKind);
	if (!Class
		|| Class->ClassRoot.Get() != Object.GetClass()
		|| !Manifest.Data->Providers.IsValidIndex(
			Class->ProviderIndex))
	{
		OutError = FString::Printf(
			TEXT("Pool class '%s' is absent from the locally frozen manifest."),
			*Object.GetClass()->GetPathName());
		return false;
	}
	const FSeinPoolObjectCodecManifest::FData::FProvider&
		Provider = Manifest.Data->Providers[Class->ProviderIndex];

	FSeinStateProviderTransactionScope Transaction;
	FProviderClaim Claim;
	if (!ResolveClaim(Provider.Token, Claim, OutError)
		|| Claim.DescriptorDigest != Provider.DescriptorDigest)
	{
		return false;
	}
	TArray<uint8> StateBytes;
	if (!Claim.Ops.Capture(
			{ Object }, StateBytes, OutError)
		|| StateBytes.Num() > Provider.Descriptor.MaxStateBytes)
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Pool object capture exceeded its provider bound.");
		}
		return false;
	}

	OutRecord.bAlive = true;
	OutRecord.ObjectKind =
		static_cast<uint8>(ExpectedKind);
	OutRecord.ClassPath = Class->ExactClassPath;
	OutRecord.NativeAnchorClassPath =
		Provider.Descriptor.NativeAnchor->GetPathName();
	OutRecord.StableProviderID =
		Provider.Descriptor.StableProviderId;
	OutRecord.StateSchemaVersion =
		Provider.Descriptor.StateSchemaVersion;
	OutRecord.BehaviorRevision =
		Provider.Descriptor.BehaviorRevision;
	OutRecord.CodecRevision =
		Provider.Descriptor.CodecRevision;
	OutRecord.ProviderDescriptorDigest =
		Provider.DescriptorDigest;
	OutRecord.ExactClassSchemaDigest =
		Class->ClassSchemaDigest;
	OutRecord.StateBytes = MoveTemp(StateBytes);
	return true;
}

bool FSeinPoolObjectCodecRegistry::ResolveVerifiedRootCaptureMode(
	const FSeinPoolObjectCodecManifest& Manifest,
	const UObject& Object,
	ESeinPoolObjectKind ExpectedKind,
	bool& bOutParallelReflected,
	FString& OutError)
{
	check(IsInGameThread());
	bOutParallelReflected = false;
	OutError.Reset();
	const FSeinPoolObjectCodecManifest::FData::FAdmittedClass* Class =
		FindAdmittedClass(
			Manifest.Data.Get(), Object.GetClass(), ExpectedKind);
	if (!Manifest.IsValid()
		|| !Class
		|| Class->ClassRoot.Get() != Object.GetClass()
		|| !Class->RootClassContractDigest.IsValid()
		|| !Manifest.Data->Providers.IsValidIndex(
			Class->ProviderIndex))
	{
		OutError = FString::Printf(
			TEXT("Pool class '%s' is absent from the verified local manifest."),
			*Object.GetClass()->GetPathName());
		return false;
	}
	bOutParallelReflected = Manifest.Data->Providers[
		Class->ProviderIndex].Descriptor.bUsesReflectedState;
	return true;
}

bool FSeinPoolObjectCodecRegistry::CaptureObjectForVerifiedRoot(
	const FSeinPoolObjectCodecManifest& Manifest,
	const UObject& Object,
	ESeinPoolObjectKind ExpectedKind,
	TArray<uint8>& OutStateBytes,
	FGuid& OutRootClassContractDigest,
	FString& OutError)
{
	OutStateBytes.Reset();
	OutRootClassContractDigest.Invalidate();
	OutError.Reset();
	if (!Manifest.IsValid())
	{
		OutError =
			TEXT("Root pool capture requires a verified manifest.");
		return false;
	}

	const FSeinPoolObjectCodecManifest::FData::FAdmittedClass*
		Class = FindAdmittedClass(
			Manifest.Data.Get(), Object.GetClass(), ExpectedKind);
	if (!Class
		|| Class->ClassRoot.Get() != Object.GetClass()
		|| !Class->RootClassContractDigest.IsValid()
		|| !Manifest.Data->Providers.IsValidIndex(
			Class->ProviderIndex))
	{
		OutError = FString::Printf(
			TEXT("Pool class '%s' is absent from the verified local manifest."),
			*Object.GetClass()->GetPathName());
		return false;
	}

	const FSeinPoolObjectCodecManifest::FData::FProvider& Provider =
		Manifest.Data->Providers[Class->ProviderIndex];
	if (!Provider.Ops.Capture)
	{
		OutError = TEXT("Verified pool provider has no capture callback.");
		return false;
	}

	const bool bCaptured = Provider.Descriptor.bUsesReflectedState
		? CaptureReflectedPoolObjectState(
			Object, OutStateBytes, OutError)
		: (IsInGameThread()
			&& !FSeinStateProviderTransactionScope::IsActive()
			&& [&Provider, &Object, &OutStateBytes, &OutError]()
			{
				FSeinStateProviderTransactionScope Transaction;
				return Provider.Ops.Capture(
					{ Object }, OutStateBytes, OutError);
			}());
	if (!bCaptured
		|| OutStateBytes.Num() > Provider.Descriptor.MaxStateBytes)
	{
		if (OutError.IsEmpty())
		{
			OutError = Provider.Descriptor.bUsesReflectedState
				? TEXT("Root pool object capture exceeded its provider bound.")
				: TEXT("Explicit root pool providers require the game-thread callback boundary.");
		}
		OutStateBytes.Reset();
		return false;
	}

	OutRootClassContractDigest = Class->RootClassContractDigest;
	return true;
}

UObject* FSeinPoolObjectCodecRegistry::MaterializeObject(
	const FSeinPoolObjectCodecManifest& Manifest,
	const FSeinSnapshotPoolInstanceRecord& Record,
	ESeinPoolObjectKind ExpectedKind,
	UObject& FinalOuter,
	FString& OutError)
{
	check(IsInGameThread());
	OutError.Reset();
	if (!Manifest.IsValid()
		|| FSeinStateProviderTransactionScope::IsActive()
		|| !Record.bAlive
		|| Record.ObjectKind != static_cast<uint8>(ExpectedKind))
	{
		OutError =
			TEXT("Pool materialization requires a live, correctly typed record and valid local manifest.");
		return nullptr;
	}
	const FSeinPoolObjectCodecManifest::FData::FAdmittedClass*
		Class = FindAdmittedClass(
			Manifest.Data.Get(), Record.ClassPath, ExpectedKind);
	if (!Class
		|| !Manifest.Data->Providers.IsValidIndex(
			Class->ProviderIndex))
	{
		OutError = FString::Printf(
			TEXT("Imported pool class '%s' is not locally admitted."),
			*Record.ClassPath);
		return nullptr;
	}
	const FSeinPoolObjectCodecManifest::FData::FProvider&
		Provider = Manifest.Data->Providers[Class->ProviderIndex];
	const FSeinPoolObjectCodecDescriptor& Descriptor =
		Provider.Descriptor;
	if (!Class->ClassRoot.IsValid()
		|| Class->ClassRoot->GetPathName() != Record.ClassPath
		|| Descriptor.NativeAnchor->GetPathName()
			!= Record.NativeAnchorClassPath
		|| Descriptor.StableProviderId
			!= Record.StableProviderID
		|| Descriptor.StateSchemaVersion
			!= Record.StateSchemaVersion
		|| Descriptor.BehaviorRevision
			!= Record.BehaviorRevision
		|| Descriptor.CodecRevision != Record.CodecRevision
		|| Provider.DescriptorDigest
			!= Record.ProviderDescriptorDigest
		|| Class->ClassSchemaDigest
			!= Record.ExactClassSchemaDigest
		|| Record.StateBytes.Num() > Descriptor.MaxStateBytes)
	{
		OutError = FString::Printf(
			TEXT("Imported pool descriptor for '%s' does not match the local frozen provider."),
			*Record.ClassPath);
		return nullptr;
	}

	FSeinStateProviderTransactionScope Transaction;
	FProviderClaim Claim;
	if (!ResolveClaim(Provider.Token, Claim, OutError)
		|| Claim.DescriptorDigest != Provider.DescriptorDigest)
	{
		return nullptr;
	}
	UObject* Candidate = Claim.Ops.Materialize(
		{
			FinalOuter,
			*Class->ClassRoot.Get(),
			Record.StateBytes,
		},
		OutError);
	if (!Candidate
		|| Candidate->GetClass() != Class->ClassRoot.Get()
		|| Candidate->GetOuter() != &FinalOuter
		|| Candidate->HasAnyFlags(
			RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("Pool provider returned no candidate or violated the exact class/final Outer contract.");
		}
		return nullptr;
	}
	return Candidate;
}

FSeinPoolObjectCodecOps
FSeinPoolObjectCodecRegistry::MakeReflectedOps()
{
	FSeinPoolObjectCodecOps Ops;
	Ops.Capture = [](
		const FSeinPoolObjectCaptureContext& Context,
		TArray<uint8>& OutBytes,
		FString& OutError)
	{
		return CaptureReflectedPoolObjectState(
			Context.Object, OutBytes, OutError);
	};
	Ops.Materialize = [](
		const FSeinPoolObjectMaterializeContext& Context,
		FString& OutError) -> UObject*
	{
		OutError.Reset();
		int32 Cursor = 0;
		uint32 Magic = 0;
		uint32 Version = 0;
		uint32 AbilityMarker = 0;
		uint32 ReflectedLength = 0;
		const bool bExpectedAbility =
			Context.ExactClass.IsChildOf(
				USeinAbility::StaticClass());
		if (!ReadUInt32(Context.StateBytes, Cursor, Magic)
			|| !ReadUInt32(Context.StateBytes, Cursor, Version)
			|| !ReadUInt32(
				Context.StateBytes, Cursor, AbilityMarker)
			|| !ReadUInt32(
				Context.StateBytes, Cursor, ReflectedLength)
			|| Magic != 0x53504f52
			|| Version != ReflectedEnvelopeVersion
			|| AbilityMarker != (bExpectedAbility ? 1u : 0u)
			|| ReflectedLength
				> static_cast<uint32>(
					Context.StateBytes.Num() - Cursor))
		{
			OutError =
				TEXT("Reflected pool envelope header is invalid.");
			return nullptr;
		}
		const TConstArrayView<uint8> ReflectedBytes =
			Context.StateBytes.Slice(
				Cursor, static_cast<int32>(ReflectedLength));
		Cursor += static_cast<int32>(ReflectedLength);

		FSeinResourceCost DeductedCost;
		FSeinResourceCost PendingCompletionCost;
		if (bExpectedAbility)
		{
			if (!ReadResourceCost(
					Context.StateBytes,
					Cursor,
					DeductedCost,
					OutError)
				|| !ReadResourceCost(
					Context.StateBytes,
					Cursor,
					PendingCompletionCost,
					OutError))
			{
				return nullptr;
			}
		}
		if (Cursor != Context.StateBytes.Num())
		{
			OutError =
				TEXT("Reflected pool envelope has trailing bytes.");
			return nullptr;
		}

		UObject* Candidate = NewObject<UObject>(
			&Context.FinalOuter,
			const_cast<UClass*>(&Context.ExactClass),
			NAME_None,
			RF_Transient);
		if (!Candidate)
		{
			OutError =
				TEXT("Reflected pool candidate construction failed.");
			return nullptr;
		}
		if (!FSeinCanonicalStateCodec::DecodeObject(
				ReflectedBytes,
				*Candidate,
				{},
				MakeReflectedWireLimits(),
				&ShouldSerializeReflectedProperty,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError =
					TEXT("Reflected pool state failed bounded deserialization.");
			}
			return nullptr;
		}
		if (USeinAbility* Ability =
			Cast<USeinAbility>(Candidate))
		{
			Ability->DeductedCost = MoveTemp(DeductedCost);
			Ability->PendingCompletionCost =
				MoveTemp(PendingCompletionCost);
		}
		return Candidate;
	};
	return Ops;
}

bool FSeinPoolObjectCodecRegistry::ValidateReflectedClassSchema(
	const UClass* ExactClass,
	FString& OutError)
{
	OutError.Reset();
	if (!ExactClass)
	{
		OutError = TEXT("Reflected pool schema requires a class.");
		return false;
	}
	return ValidateReflectedClassSchemaInternal(
		*ExactClass, OutError);
}

#if WITH_DEV_AUTOMATION_TESTS
FSeinPoolObjectLocalClassAdmissionHandle
FSeinPoolObjectCodecRegistry::RegisterExplicitLocalClassForTests(
	const UClass* ExactClass,
	FString* OutError)
{
	check(IsInGameThread());
	SetError(OutError, FString());
	if (!IsConcreteClass(ExactClass)
		|| !ExactClass->HasAnyClassFlags(
			CLASS_CompiledFromBlueprint)
		|| FSeinStateProviderTransactionScope::IsActive())
	{
		SetError(
			OutError,
			TEXT("Explicit test admission requires a concrete transient Blueprint class outside provider callbacks."));
		return {};
	}
	const UClass* NativeAnchor =
		FindNearestNativeAnchor(ExactClass);
	const FProviderEntry* Provider = NativeAnchor
		? Registry().FindByPredicate(
			[NativeAnchor](const FProviderEntry& Entry)
			{
				return Entry.AnchorPath
						== NativeAnchor->GetPathName()
					&& !Entry.Claims.IsEmpty()
					&& Entry.Claims.Last().Descriptor
						.bAllowBlueprintChildren;
			})
		: nullptr;
	FString Error;
	if (!Provider
		|| (Provider->Claims.Last().Descriptor.bUsesReflectedState
			&& !ValidateReflectedClassSchemaInternal(
				*ExactClass, Error)))
	{
		SetError(
			OutError,
			Error.IsEmpty()
				? TEXT("The test Blueprint's nearest native anchor has no inheritable pool provider.")
				: MoveTemp(Error));
		return {};
	}
	if (TestLocalClassAdmissions().ContainsByPredicate(
		[ExactClass](const FTestLocalClassAdmission& Existing)
		{
			return Existing.ClassRoot.Get() == ExactClass;
		}))
	{
		SetError(
			OutError,
			TEXT("The test Blueprint class already has an explicit local admission lease."));
		return {};
	}
	FTestLocalClassAdmission& Admission =
		TestLocalClassAdmissions().AddDefaulted_GetRef();
	Admission.Token = AllocateToken();
	Admission.ClassRoot.Reset(const_cast<UClass*>(ExactClass));
	return FSeinPoolObjectLocalClassAdmissionHandle(
		Admission.Token);
}

bool FSeinPoolObjectCodecRegistry::UnregisterTestLocalClass(
	uint64 Token)
{
	check(IsInGameThread());
	if (Token == 0
		|| FSeinStateProviderTransactionScope::IsActive())
	{
		return false;
	}
	const int32 Index =
		TestLocalClassAdmissions().IndexOfByPredicate(
			[Token](const FTestLocalClassAdmission& Admission)
			{
				return Admission.Token == Token;
			});
	if (Index == INDEX_NONE)
	{
		return false;
	}
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (It->HasAnyFlags(RF_ClassDefaultObject)
			|| !It->PoolObjectCodecManifest.Data.IsValid())
		{
			continue;
		}
		const bool bUsesAdmission =
			It->PoolObjectCodecManifest.Data->Classes
				.ContainsByPredicate(
					[Token](
						const FSeinPoolObjectCodecManifest::
							FData::FAdmittedClass& Class)
					{
						return Class.TestAdmissionToken == Token;
					});
		if (bUsesAdmission)
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSTests"),
				TEXT("an explicit transient pool-class admission ended"));
		}
	}
	TestLocalClassAdmissions().RemoveAt(Index);
	return true;
}
#endif

int32 FSeinPoolObjectCodecRegistry::GetRegisteredProviderCount()
{
	check(IsInGameThread());
	return Registry().Num();
}
