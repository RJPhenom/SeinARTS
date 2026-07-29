/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandSchemaRegistry.cpp
 */

#include "Input/SeinCommandSchemaRegistry.h"

#include "GameplayTagContainer.h"
#include "Hash/Blake3.h"
#include "Misc/ScopeLock.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Class.h"
#include "UObject/PropertyOptional.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCommandSchema, Log, All);

namespace
{
	struct FCommandSchemaRegistrationClaim
	{
		FCommandSchemaRegistrationClaim(
			uint64 InToken,
			UScriptStruct* InPayloadStruct,
			UClass* InHandlerClass,
			TConstArrayView<const UScriptStruct*> InDynamicPayloadStructs)
			: Token(InToken)
			, PayloadStruct(InPayloadStruct)
			, HandlerClass(InHandlerClass)
		{
			DynamicPayloadStructs.Reserve(InDynamicPayloadStructs.Num());
			for (const UScriptStruct* Struct : InDynamicPayloadStructs)
			{
				DynamicPayloadStructs.Emplace(const_cast<UScriptStruct*>(Struct));
			}
		}

		uint64 Token = 0;
		TStrongObjectPtr<UScriptStruct> PayloadStruct;
		TStrongObjectPtr<UClass> HandlerClass;
		TArray<TStrongObjectPtr<UScriptStruct>> DynamicPayloadStructs;
	};

	struct FRegisteredCommandSchema
	{
		FName OwnerId;
		FName StableSchemaId;
		FGameplayTag CommandType;
		int32 SchemaVersion = 0;
		int32 ImplementationRevision = 0;
		FString PayloadStructPath;
		FString PayloadLayoutManifest;
		FString DynamicPayloadManifest;
		TArray<FName> AllowedPayloadNames;
		FString AllowedPayloadNameManifest;
		ESeinCommandAuthorityScope AuthorityScope = ESeinCommandAuthorityScope::Entity;
		int32 MaxEntityListEntries = 0;
		int32 MaxTargeterPoints = 0;
		int32 MaxPayloadBytes = 0;
		int32 MaxPayloadAggregateElements = 0;
		int32 AllowedExecutionContexts = 0;
		FString HandlerClassPath;
		TArray<FCommandSchemaRegistrationClaim> RegistrationClaims;
	};

	TArray<FRegisteredCommandSchema>& GetRegistry()
	{
		static TArray<FRegisteredCommandSchema> Registry;
		return Registry;
	}

	FCriticalSection& GetRegistryMutex()
	{
		static FCriticalSection Mutex;
		return Mutex;
	}

	uint64& GetNextRegistrationToken()
	{
		static uint64 NextToken = 1;
		return NextToken;
	}

	FString GetPayloadStructPath(const UScriptStruct* PayloadStruct)
	{
		return PayloadStruct ? PayloadStruct->GetPathName() : FString();
	}

	FString GetHandlerClassPath(const TSubclassOf<USeinCommandHandler>& HandlerClass)
	{
		return HandlerClass ? HandlerClass->GetPathName() : FString();
	}

	bool HasExactKey(
		const FRegisteredCommandSchema& Registered,
		FGameplayTag CommandType,
		int32 SchemaVersion)
	{
		return Registered.CommandType == CommandType
			&& Registered.SchemaVersion == SchemaVersion;
	}

	bool IsExactDuplicate(
		const FRegisteredCommandSchema& Registered,
		FName OwnerId,
		const FSeinCommandSchemaDescriptor& Descriptor,
		const FString& PayloadLayoutManifest,
		const FString& DynamicPayloadManifest,
		const FString& AllowedPayloadNameManifest)
	{
		return Registered.OwnerId == OwnerId
			&& Registered.StableSchemaId == Descriptor.StableSchemaId
			&& HasExactKey(Registered, Descriptor.CommandType, Descriptor.SchemaVersion)
			&& Registered.ImplementationRevision == Descriptor.ImplementationRevision
			&& Registered.PayloadStructPath == GetPayloadStructPath(Descriptor.PayloadStruct)
			&& Registered.PayloadLayoutManifest == PayloadLayoutManifest
			&& Registered.DynamicPayloadManifest == DynamicPayloadManifest
			&& Registered.AllowedPayloadNameManifest == AllowedPayloadNameManifest
			&& Registered.AuthorityScope == Descriptor.AuthorityScope
			&& Registered.MaxEntityListEntries == Descriptor.MaxEntityListEntries
			&& Registered.MaxTargeterPoints == Descriptor.MaxTargeterPoints
			&& Registered.MaxPayloadBytes == Descriptor.MaxPayloadBytes
			&& Registered.MaxPayloadAggregateElements == Descriptor.MaxPayloadAggregateElements
			&& Registered.AllowedExecutionContexts == Descriptor.AllowedExecutionContexts
			&& Registered.HandlerClassPath == GetHandlerClassPath(Descriptor.HandlerClass);
	}

	bool IsKnownAuthorityScope(ESeinCommandAuthorityScope Scope)
	{
		switch (Scope)
		{
		case ESeinCommandAuthorityScope::PublicObserver:
		case ESeinCommandAuthorityScope::Self:
		case ESeinCommandAuthorityScope::Entity:
		case ESeinCommandAuthorityScope::EntitySet:
		case ESeinCommandAuthorityScope::MatchControl:
		case ESeinCommandAuthorityScope::DerivedSystem:
			return true;
		default:
			return false;
		}
	}

	uint64 AllocateRegistrationToken()
	{
		uint64& NextToken = GetNextRegistrationToken();
		if (NextToken == 0)
		{
			NextToken = 1;
		}
		return NextToken++;
	}

	void CopyDescriptor(
		const FRegisteredCommandSchema& Registered,
		FSeinCommandSchemaDescriptor& OutDescriptor)
	{
		check(!Registered.RegistrationClaims.IsEmpty());
		const FCommandSchemaRegistrationClaim& ActiveClaim =
			Registered.RegistrationClaims.Last();
		OutDescriptor.StableSchemaId = Registered.StableSchemaId;
		OutDescriptor.CommandType = Registered.CommandType;
		OutDescriptor.SchemaVersion = Registered.SchemaVersion;
		OutDescriptor.ImplementationRevision = Registered.ImplementationRevision;
		OutDescriptor.PayloadStruct = ActiveClaim.PayloadStruct.Get();
		OutDescriptor.DynamicPayloadStructs.Reset(ActiveClaim.DynamicPayloadStructs.Num());
		for (const TStrongObjectPtr<UScriptStruct>& Struct : ActiveClaim.DynamicPayloadStructs)
		{
			OutDescriptor.DynamicPayloadStructs.Add(Struct.Get());
		}
		OutDescriptor.AllowedPayloadNames = Registered.AllowedPayloadNames;
		OutDescriptor.AuthorityScope = Registered.AuthorityScope;
		OutDescriptor.MaxEntityListEntries = Registered.MaxEntityListEntries;
		OutDescriptor.MaxTargeterPoints = Registered.MaxTargeterPoints;
		OutDescriptor.MaxPayloadBytes = Registered.MaxPayloadBytes;
		OutDescriptor.MaxPayloadAggregateElements = Registered.MaxPayloadAggregateElements;
		OutDescriptor.AllowedExecutionContexts = Registered.AllowedExecutionContexts;
		OutDescriptor.HandlerClass = ActiveClaim.HandlerClass.Get();
	}

	const TCHAR* AuthorityScopeToken(ESeinCommandAuthorityScope Scope)
	{
		switch (Scope)
		{
		case ESeinCommandAuthorityScope::PublicObserver: return TEXT("PublicObserver");
		case ESeinCommandAuthorityScope::Self:           return TEXT("Self");
		case ESeinCommandAuthorityScope::Entity:         return TEXT("Entity");
		case ESeinCommandAuthorityScope::EntitySet:      return TEXT("EntitySet");
		case ESeinCommandAuthorityScope::MatchControl:   return TEXT("MatchControl");
		case ESeinCommandAuthorityScope::DerivedSystem:  return TEXT("DerivedSystem");
		default:                                         return TEXT("Invalid");
		}
	}

	FString ExecutionAllowanceToken(int32 Allowances)
	{
		FString Result;
		auto Append = [&Result](const TCHAR* Token)
		{
			if (!Result.IsEmpty())
			{
				Result += TEXT("|");
			}
			Result += Token;
		};

		if ((Allowances & static_cast<int32>(ESeinCommandExecutionAllowance::Spectator)) != 0)
		{
			Append(TEXT("Spectator"));
		}
		if ((Allowances & static_cast<int32>(ESeinCommandExecutionAllowance::HardPause)) != 0)
		{
			Append(TEXT("HardPause"));
		}
		if ((Allowances & static_cast<int32>(ESeinCommandExecutionAllowance::Starting)) != 0)
		{
			Append(TEXT("Starting"));
		}
		if ((Allowances & static_cast<int32>(ESeinCommandExecutionAllowance::FrozenPauseControl)) != 0)
		{
			Append(TEXT("FrozenPauseControl"));
		}
		return Result.IsEmpty() ? TEXT("None") : Result;
	}

	void AppendFrame(FString& Out, const FString& Value)
	{
		Out += FString::FromInt(Value.Len());
		Out += TEXT(":");
		Out += Value;
	}

	FString CanonicalNameBase(FName Name)
	{
		FString Base = Name.GetPlainNameString();
		// Framework identifiers are ASCII. Fold explicitly so catalog identity is
		// independent of process locale as well as FName display casing/load order.
		for (TCHAR& Character : Base)
		{
			if (Character >= TCHAR('A') && Character <= TCHAR('Z'))
			{
				Character += TCHAR('a') - TCHAR('A');
			}
		}
		return Base;
	}

	FString CanonicalNameIdentity(FName Name)
	{
		FString Identity;
		AppendFrame(Identity, CanonicalNameBase(Name));
		AppendFrame(Identity, FString::FromInt(Name.GetNumber()));
		return Identity;
	}

	void BuildCanonicalWireNameCatalogImpl(
		TConstArrayView<FName> Names,
		TArray<FName>& OutCanonicalNames,
		FString& OutCanonicalManifest)
	{
		OutCanonicalNames.Reset();
		OutCanonicalNames.Reserve(Names.Num());
		for (const FName Name : Names)
		{
			if (!Name.IsNone())
			{
				const FString Base = CanonicalNameBase(Name);
				OutCanonicalNames.Emplace(*Base, Name.GetNumber());
			}
		}
		OutCanonicalNames.Sort([](const FName A, const FName B)
		{
			return CanonicalNameIdentity(A).Compare(
				CanonicalNameIdentity(B), ESearchCase::CaseSensitive) < 0;
		});
		for (int32 Index = OutCanonicalNames.Num() - 1; Index > 0; --Index)
		{
			if (OutCanonicalNames[Index] == OutCanonicalNames[Index - 1])
			{
				OutCanonicalNames.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}

		OutCanonicalManifest = FString::Printf(
			TEXT("WireNames|1|%d|"), OutCanonicalNames.Num());
		for (const FName Name : OutCanonicalNames)
		{
			OutCanonicalManifest += TEXT("N[");
			OutCanonicalManifest += CanonicalNameIdentity(Name);
			OutCanonicalManifest += TEXT("]");
		}
	}

	uint32 ReadUInt32BigEndian(const uint8* Bytes)
	{
		return static_cast<uint32>(Bytes[0]) << 24
			| static_cast<uint32>(Bytes[1]) << 16
			| static_cast<uint32>(Bytes[2]) << 8
			| static_cast<uint32>(Bytes[3]);
	}

	struct FCanonicalCommandSchema
	{
		FString CommandType;
		int32 SchemaVersion = 0;
		int32 ImplementationRevision = 0;
		FString OwnerId;
		FString StableSchemaId;
		FString PayloadStructPath;
		FString PayloadLayoutManifest;
		FString DynamicPayloadManifest;
		FString AllowedPayloadNameManifest;
		FString AuthorityScope;
		int32 MaxEntityListEntries = 0;
		int32 MaxTargeterPoints = 0;
		int32 MaxPayloadBytes = 0;
		int32 MaxPayloadAggregateElements = 0;
		FString AllowedExecutionContexts;
		FString HandlerClassPath;
	};

	FCanonicalCommandSchema MakeCanonicalSchema(
		const FRegisteredCommandSchema& Registered)
	{
		FCanonicalCommandSchema Schema;
		Schema.CommandType = CanonicalNameIdentity(Registered.CommandType.GetTagName());
		Schema.SchemaVersion = Registered.SchemaVersion;
		Schema.ImplementationRevision = Registered.ImplementationRevision;
		Schema.OwnerId = CanonicalNameIdentity(Registered.OwnerId);
		Schema.StableSchemaId = CanonicalNameIdentity(Registered.StableSchemaId);
		Schema.PayloadStructPath = Registered.PayloadStructPath.IsEmpty()
			? TEXT("<none>") : Registered.PayloadStructPath;
		Schema.PayloadLayoutManifest = Registered.PayloadLayoutManifest;
		Schema.DynamicPayloadManifest = Registered.DynamicPayloadManifest;
		Schema.AllowedPayloadNameManifest = Registered.AllowedPayloadNameManifest;
		Schema.AuthorityScope = AuthorityScopeToken(Registered.AuthorityScope);
		Schema.MaxEntityListEntries = Registered.MaxEntityListEntries;
		Schema.MaxTargeterPoints = Registered.MaxTargeterPoints;
		Schema.MaxPayloadBytes = Registered.MaxPayloadBytes;
		Schema.MaxPayloadAggregateElements = Registered.MaxPayloadAggregateElements;
		Schema.AllowedExecutionContexts =
			ExecutionAllowanceToken(Registered.AllowedExecutionContexts);
		Schema.HandlerClassPath = Registered.HandlerClassPath;
		return Schema;
	}

	FString BuildCanonicalManifestFromSchemas(
		TArray<FCanonicalCommandSchema> Schemas,
		const FString& AdditionalDynamicPayloadManifest = TEXT("DynamicPayloads|1|0|"),
		const FString& AdditionalWireNameManifest = TEXT("WireNames|1|0|"))
	{
		Schemas.Sort([](const FCanonicalCommandSchema& A, const FCanonicalCommandSchema& B)
		{
			const int32 TagOrder = A.CommandType.Compare(B.CommandType, ESearchCase::CaseSensitive);
			if (TagOrder != 0)
			{
				return TagOrder < 0;
			}
			if (A.SchemaVersion != B.SchemaVersion)
			{
				return A.SchemaVersion < B.SchemaVersion;
			}
			return A.StableSchemaId.Compare(B.StableSchemaId, ESearchCase::CaseSensitive) < 0;
		});

		FString Manifest = FString::Printf(TEXT("SeinCommandSchemas|3|%d|"), Schemas.Num());
		for (const FCanonicalCommandSchema& Schema : Schemas)
		{
			Manifest += TEXT("S[");
			AppendFrame(Manifest, Schema.CommandType);
			AppendFrame(Manifest, FString::FromInt(Schema.SchemaVersion));
			AppendFrame(Manifest, FString::FromInt(Schema.ImplementationRevision));
			AppendFrame(Manifest, Schema.OwnerId);
			AppendFrame(Manifest, Schema.StableSchemaId);
			AppendFrame(Manifest, Schema.PayloadStructPath);
			AppendFrame(Manifest, Schema.PayloadLayoutManifest);
			AppendFrame(Manifest, Schema.DynamicPayloadManifest);
			AppendFrame(Manifest, Schema.AllowedPayloadNameManifest);
			AppendFrame(Manifest, Schema.AuthorityScope);
			AppendFrame(Manifest, FString::FromInt(Schema.MaxEntityListEntries));
			AppendFrame(Manifest, FString::FromInt(Schema.MaxTargeterPoints));
			AppendFrame(Manifest, FString::FromInt(Schema.MaxPayloadBytes));
			AppendFrame(Manifest, FString::FromInt(Schema.MaxPayloadAggregateElements));
			AppendFrame(Manifest, Schema.AllowedExecutionContexts);
			AppendFrame(Manifest, Schema.HandlerClassPath);
			Manifest += TEXT("]");
		}
		Manifest += TEXT("G[");
		AppendFrame(Manifest, AdditionalDynamicPayloadManifest);
		AppendFrame(Manifest, AdditionalWireNameManifest);
		Manifest += TEXT("]");
		return Manifest;
	}

	FGuid ComputeManifestDigest(const FString& Manifest)
	{
		FTCHARToUTF8 Utf8(*Manifest);
		const FBlake3Hash Hash = FBlake3::HashBuffer(Utf8.Get(), Utf8.Length());
		const uint8* Bytes = Hash.GetBytes();
		return FGuid(
			ReadUInt32BigEndian(Bytes),
			ReadUInt32BigEndian(Bytes + 4),
			ReadUInt32BigEndian(Bytes + 8),
			ReadUInt32BigEndian(Bytes + 12));
	}

	constexpr int32 KnownExecutionAllowanceMask =
		static_cast<int32>(ESeinCommandExecutionAllowance::Spectator)
		| static_cast<int32>(ESeinCommandExecutionAllowance::HardPause)
		| static_cast<int32>(ESeinCommandExecutionAllowance::Starting)
		| static_cast<int32>(ESeinCommandExecutionAllowance::FrozenPauseControl);

	enum class EPayloadWalkResult : uint8
	{
		Valid,
		NonDeterministic,
		UnsupportedField,
		NameOutsideCatalog,
		TooLarge,
	};

	bool IsKnownDeterministicEngineStruct(const UScriptStruct* Struct)
	{
		return Struct == FGameplayTag::StaticStruct()
			|| Struct == FGameplayTagContainer::StaticStruct();
	}

	bool AllowsInstancedStruct(const FProperty& Property, bool bInheritedAllowance = false)
	{
#if WITH_METADATA
		static const FName DeterministicOnlyMeta(TEXT("SeinDeterministicOnly"));
		return bInheritedAllowance || Property.HasMetaData(DeterministicOnlyMeta);
#else
		// The editor validates the authoring declaration before cook; runtime still
		// validates every concrete nested instance and its aggregate budget.
		(void)Property;
		return true;
#endif
	}

	EPayloadWalkResult ValidatePayloadStructType(
		const UScriptStruct* Struct,
		TSet<const UScriptStruct*>& Visiting);

	EPayloadWalkResult ValidatePayloadPropertyType(
		const FProperty& Property,
		bool bInstancedStructAllowed,
		TSet<const UScriptStruct*>& Visiting)
	{
		if (Property.HasAnyPropertyFlags(
			CPF_Transient | CPF_EditorOnly | CPF_Deprecated | CPF_SkipSerialization))
		{
			return EPayloadWalkResult::UnsupportedField;
		}

		const bool bAllowNestedInstanced = AllowsInstancedStruct(Property, bInstancedStructAllowed);
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(&Property))
		{
			return ValidatePayloadPropertyType(*ArrayProperty->Inner, bAllowNestedInstanced, Visiting);
		}
		if (Property.IsA<FSetProperty>() || Property.IsA<FMapProperty>())
		{
			// Hash-bucket iteration is not a cross-process ordering contract (FName
			// indices are a common source of differing bucket layouts). A command
			// handler must observe one canonical sequence, so unordered containers
			// are forbidden at the wire-schema boundary. Author an array of explicit
			// key/value records and sort it before submission instead.
			return EPayloadWalkResult::UnsupportedField;
		}
		if (const FOptionalProperty* OptionalProperty = CastField<FOptionalProperty>(&Property))
		{
			return ValidatePayloadPropertyType(
				*OptionalProperty->GetValueProperty(), bAllowNestedInstanced, Visiting);
		}

		if (Property.IsA<FBoolProperty>()
			|| Property.IsA<FByteProperty>()
			|| Property.IsA<FInt8Property>()
			|| Property.IsA<FInt16Property>()
			|| Property.IsA<FIntProperty>()
			|| Property.IsA<FInt64Property>()
			|| Property.IsA<FUInt16Property>()
			|| Property.IsA<FUInt32Property>()
			|| Property.IsA<FUInt64Property>()
			|| Property.IsA<FEnumProperty>()
			|| Property.IsA<FNameProperty>())
		{
			return EPayloadWalkResult::Valid;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				return bAllowNestedInstanced
					? EPayloadWalkResult::Valid
					: EPayloadWalkResult::UnsupportedField;
			}
			return ValidatePayloadStructType(StructProperty->Struct, Visiting);
		}

		// Float/double, FString/FText, object/class/soft/weak/interface refs,
		// delegates, field paths, and every unknown property kind are forbidden.
		return EPayloadWalkResult::UnsupportedField;
	}

	EPayloadWalkResult ValidatePayloadStructType(
		const UScriptStruct* Struct,
		TSet<const UScriptStruct*>& Visiting)
	{
		if (!Struct || Struct == FInstancedStruct::StaticStruct())
		{
			return EPayloadWalkResult::UnsupportedField;
		}
		if (IsKnownDeterministicEngineStruct(Struct))
		{
			return EPayloadWalkResult::Valid;
		}
		if (!FSeinCommandSchemaRegistry::IsDeterministicPayloadStruct(Struct))
		{
			return EPayloadWalkResult::NonDeterministic;
		}
		if (Visiting.Contains(Struct))
		{
			return EPayloadWalkResult::Valid;
		}

		Visiting.Add(Struct);
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const EPayloadWalkResult Result = ValidatePayloadPropertyType(**It, false, Visiting);
			if (Result != EPayloadWalkResult::Valid)
			{
				Visiting.Remove(Struct);
				return Result;
			}
		}
		Visiting.Remove(Struct);
		return EPayloadWalkResult::Valid;
	}

	EPayloadWalkResult ValidatePayloadStructType(const UScriptStruct* Struct)
	{
		TSet<const UScriptStruct*> Visiting;
		return ValidatePayloadStructType(Struct, Visiting);
	}

	FString PropertyOwnerPath(const FProperty& Property)
	{
		const UStruct* Owner = Property.GetOwnerStruct();
		return Owner ? Owner->GetPathName() : TEXT("<none>");
	}

	bool BuildIntegerLayout(const FNumericProperty& Property, FString& Out)
	{
		if (Property.IsA<FByteProperty>()) Out = TEXT("UInt8");
		else if (Property.IsA<FInt8Property>()) Out = TEXT("Int8");
		else if (Property.IsA<FInt16Property>()) Out = TEXT("Int16");
		else if (Property.IsA<FUInt16Property>()) Out = TEXT("UInt16");
		else if (Property.IsA<FIntProperty>()) Out = TEXT("Int32");
		else if (Property.IsA<FUInt32Property>()) Out = TEXT("UInt32");
		else if (Property.IsA<FInt64Property>()) Out = TEXT("Int64");
		else if (Property.IsA<FUInt64Property>()) Out = TEXT("UInt64");
		else return false;
		return true;
	}

	FString BuildEnumLayout(const UEnum* Enum)
	{
		if (!Enum)
		{
			return TEXT("<none>");
		}

		struct FEntry
		{
			FString Name;
			int64 Value = 0;
		};
		TArray<FEntry> Entries;
		Entries.Reserve(Enum->NumEnums());
		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			Entries.Add({ Enum->GetNameStringByIndex(Index), Enum->GetValueByIndex(Index) });
		}
		Entries.Sort([](const FEntry& A, const FEntry& B)
		{
			const int32 NameOrder = A.Name.Compare(B.Name, ESearchCase::CaseSensitive);
			return NameOrder != 0 ? NameOrder < 0 : A.Value < B.Value;
		});

		FString Result = TEXT("Enum|1|");
		AppendFrame(Result, Enum->GetPathName());
		AppendFrame(Result, FString::FromInt(Entries.Num()));
		for (const FEntry& Entry : Entries)
		{
			AppendFrame(Result, Entry.Name);
			AppendFrame(Result, LexToString(Entry.Value));
		}
		return Result;
	}

	bool BuildPayloadStructLayout(
		const UScriptStruct* Struct,
		FString& Out,
		TSet<const UScriptStruct*>& Visiting);

	bool BuildPayloadPropertyLayout(
		const FProperty& Property,
		FString& Out,
		TSet<const UScriptStruct*>& Visiting)
	{
		if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
		{
			FString Inner;
			if (!BuildPayloadPropertyLayout(*Array->Inner, Inner, Visiting)) return false;
			Out = TEXT("Array|");
			AppendFrame(Out, Inner);
			return true;
		}
		if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(&Property))
		{
			FString Inner;
			if (!BuildPayloadPropertyLayout(
				*Optional->GetValueProperty(), Inner, Visiting)) return false;
			Out = TEXT("Optional|");
			AppendFrame(Out, Inner);
			return true;
		}
		if (Property.IsA<FSetProperty>() || Property.IsA<FMapProperty>())
		{
			return false;
		}
		if (Property.IsA<FBoolProperty>())
		{
			Out = TEXT("Bool");
			return true;
		}
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(&Property))
		{
			FString Underlying;
			if (!BuildIntegerLayout(*Enum->GetUnderlyingProperty(), Underlying)) return false;
			Out = TEXT("EnumProperty|");
			AppendFrame(Out, Underlying);
			AppendFrame(Out, BuildEnumLayout(Enum->GetEnum()));
			return true;
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(&Property))
		{
			if (Numeric->IsFloatingPoint() || !Numeric->IsInteger()
				|| !BuildIntegerLayout(*Numeric, Out)) return false;
			if (const UEnum* Enum = Numeric->GetIntPropertyEnum())
			{
				FString NumericLayout = MoveTemp(Out);
				Out = TEXT("NumericEnum|");
				AppendFrame(Out, NumericLayout);
				AppendFrame(Out, BuildEnumLayout(Enum));
			}
			return true;
		}
		if (Property.IsA<FNameProperty>())
		{
			Out = TEXT("NameCatalogIndex32");
			return true;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				Out = TEXT("DynamicInstancedStruct");
				return true;
			}
			FString Nested;
			if (!BuildPayloadStructLayout(StructProperty->Struct, Nested, Visiting)) return false;
			Out = TEXT("Struct|");
			AppendFrame(Out, Nested);
			return true;
		}
		return false;
	}

	bool BuildPayloadStructLayout(
		const UScriptStruct* Struct,
		FString& Out,
		TSet<const UScriptStruct*>& Visiting)
	{
		if (!Struct) return false;
		if (Struct == FGameplayTag::StaticStruct())
		{
			Out = TEXT("Special|GameplayTag|1");
			return true;
		}
		if (Struct == FGameplayTagContainer::StaticStruct())
		{
			Out = TEXT("Special|GameplayTagContainer|1");
			return true;
		}
		if (Visiting.Contains(Struct))
		{
			Out = TEXT("RecursiveRef|");
			AppendFrame(Out, Struct->GetPathName());
			return true;
		}

		Visiting.Add(Struct);
		struct FPropertyLayoutEntry
		{
			const FProperty* Property = nullptr;
			int32 SerializationOrdinal = INDEX_NONE;
		};
		TArray<FPropertyLayoutEntry> Properties;
		int32 SerializationOrdinal = 0;
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			// FInstancedStruct's UE net serializer builds its root FRepLayout from
			// this reflected order. Preserve that wire-significant ordinal before
			// sorting entries into their canonical name-framed presentation.
			Properties.Add({ *It, SerializationOrdinal++ });
		}
		Properties.Sort([](const FPropertyLayoutEntry& A, const FPropertyLayoutEntry& B)
		{
			const int32 OwnerOrder = PropertyOwnerPath(*A.Property).Compare(
				PropertyOwnerPath(*B.Property), ESearchCase::CaseSensitive);
			return OwnerOrder != 0
				? OwnerOrder < 0
				: A.Property->GetName().Compare(
					B.Property->GetName(), ESearchCase::CaseSensitive) < 0;
		});

		Out = TEXT("StructLayout|3|");
		AppendFrame(Out, Struct->GetPathName());
		AppendFrame(Out, FString::FromInt(Properties.Num()));
		for (const FPropertyLayoutEntry& Entry : Properties)
		{
			const FProperty* Property = Entry.Property;
			FString TypeLayout;
			if (!BuildPayloadPropertyLayout(*Property, TypeLayout, Visiting))
			{
				Visiting.Remove(Struct);
				return false;
			}
			Out += TEXT("P[");
			AppendFrame(Out, PropertyOwnerPath(*Property));
			AppendFrame(Out, Property->GetName());
			AppendFrame(Out, FString::FromInt(Property->ArrayDim));
			AppendFrame(Out, FString::FromInt(Entry.SerializationOrdinal));
			AppendFrame(Out, TypeLayout);
			Out += TEXT("]");
		}
		Visiting.Remove(Struct);
		return true;
	}

	bool BuildPayloadStructLayout(const UScriptStruct* Struct, FString& Out)
	{
		TSet<const UScriptStruct*> Visiting;
		return BuildPayloadStructLayout(Struct, Out, Visiting);
	}

	bool BuildDynamicPayloadManifest(
		TConstArrayView<const UScriptStruct*> Structs,
		FString& OutManifest,
		TArray<const UScriptStruct*>& OutCanonicalStructs)
	{
		struct FContract
		{
			const UScriptStruct* Struct = nullptr;
			FString Path;
			FString Layout;
		};
		TArray<FContract> Contracts;
		Contracts.Reserve(Structs.Num());
		for (const UScriptStruct* Struct : Structs)
		{
			FString Layout;
			if (!Struct
				|| Struct == FInstancedStruct::StaticStruct()
				|| ValidatePayloadStructType(Struct) != EPayloadWalkResult::Valid
				|| !BuildPayloadStructLayout(Struct, Layout))
			{
				return false;
			}
			Contracts.Add({ Struct, Struct->GetPathName(), MoveTemp(Layout) });
		}
		Contracts.Sort([](const FContract& A, const FContract& B)
		{
			const int32 PathOrder = A.Path.Compare(B.Path, ESearchCase::CaseSensitive);
			return PathOrder != 0
				? PathOrder < 0
				: A.Layout.Compare(B.Layout, ESearchCase::CaseSensitive) < 0;
		});

		OutCanonicalStructs.Reset(Contracts.Num());
		FString EntriesManifest;
		int32 UniqueCount = 0;
		FString PreviousPath;
		FString PreviousLayout;
		for (const FContract& Contract : Contracts)
		{
			if (Contract.Path == PreviousPath)
			{
				if (Contract.Layout != PreviousLayout
					|| OutCanonicalStructs.IsEmpty()
					|| Contract.Struct != OutCanonicalStructs.Last()) return false;
				continue;
			}
			PreviousPath = Contract.Path;
			PreviousLayout = Contract.Layout;
			OutCanonicalStructs.Add(Contract.Struct);
			++UniqueCount;
			EntriesManifest += TEXT("D[");
			AppendFrame(EntriesManifest, Contract.Path);
			AppendFrame(EntriesManifest, Contract.Layout);
			EntriesManifest += TEXT("]");
		}
		OutManifest = FString::Printf(TEXT("DynamicPayloads|1|%d|"), UniqueCount)
			+ EntriesManifest;
		return true;
	}

	struct FPayloadBudget
	{
		explicit FPayloadBudget(int32 InMaxBytes, int32 InMaxElements)
			: MaxBytes(static_cast<uint64>(InMaxBytes))
			, MaxElements(static_cast<uint64>(InMaxElements))
		{
		}

		EPayloadWalkResult AddBytes(uint64 Amount)
		{
			if (Amount > MaxBytes || Bytes > MaxBytes - Amount)
			{
				return EPayloadWalkResult::TooLarge;
			}
			Bytes += Amount;
			return EPayloadWalkResult::Valid;
		}

		EPayloadWalkResult AddElements(uint64 Amount)
		{
			if (Amount > MaxElements || Elements > MaxElements - Amount)
			{
				return EPayloadWalkResult::TooLarge;
			}
			Elements += Amount;
			return EPayloadWalkResult::Valid;
		}

		uint64 Bytes = 0;
		uint64 Elements = 0;
		uint64 MaxBytes = 0;
		uint64 MaxElements = 0;
	};

	uint64 CanonicalIntegerPropertyBytes(const FProperty& Property)
	{
		if (Property.IsA<FByteProperty>() || Property.IsA<FInt8Property>()) return 1;
		if (Property.IsA<FInt16Property>() || Property.IsA<FUInt16Property>()) return 2;
		if (Property.IsA<FIntProperty>() || Property.IsA<FUInt32Property>()) return 4;
		if (Property.IsA<FInt64Property>() || Property.IsA<FUInt64Property>()) return 8;
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(&Property))
		{
			const FNumericProperty* Underlying = Enum->GetUnderlyingProperty();
			return Underlying ? CanonicalIntegerPropertyBytes(*Underlying) : 0;
		}
		return 0;
	}

	EPayloadWalkResult AddCanonicalStringBytes(const FString& Value, FPayloadBudget& Budget)
	{
		FTCHARToUTF8 Utf8(*Value);
		const EPayloadWalkResult PrefixResult = Budget.AddBytes(4);
		return PrefixResult == EPayloadWalkResult::Valid
			? Budget.AddBytes(static_cast<uint64>(Utf8.Length()))
			: PrefixResult;
	}

	EPayloadWalkResult WalkPayloadStructValue(
		const UScriptStruct* Struct,
		const void* Memory,
		FPayloadBudget& Budget,
		int32 Depth,
		TConstArrayView<const UScriptStruct*> AllowedDynamicPayloadStructs,
		TConstArrayView<FName> AllowedPayloadNames);

	EPayloadWalkResult WalkPayloadPropertyValue(
		const FProperty& Property,
		const void* ValuePtr,
		FPayloadBudget& Budget,
		int32 Depth,
		TConstArrayView<const UScriptStruct*> AllowedDynamicPayloadStructs,
		TConstArrayView<FName> AllowedPayloadNames)
	{
		if (Depth > 64)
		{
			return EPayloadWalkResult::TooLarge;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(&Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
			EPayloadWalkResult Result = Budget.AddBytes(4);
			if (Result == EPayloadWalkResult::Valid)
			{
				Result = Budget.AddElements(static_cast<uint64>(Helper.Num()));
			}
			for (int32 Index = 0; Result == EPayloadWalkResult::Valid && Index < Helper.Num(); ++Index)
			{
				Result = WalkPayloadPropertyValue(
					*ArrayProperty->Inner, Helper.GetRawPtr(Index), Budget, Depth + 1,
					AllowedDynamicPayloadStructs, AllowedPayloadNames);
			}
			return Result;
		}
		if (const FSetProperty* SetProperty = CastField<FSetProperty>(&Property))
		{
			FScriptSetHelper Helper(SetProperty, ValuePtr);
			EPayloadWalkResult Result = Budget.AddBytes(4);
			if (Result == EPayloadWalkResult::Valid)
			{
				Result = Budget.AddElements(static_cast<uint64>(Helper.Num()));
			}
			for (int32 Index = 0; Result == EPayloadWalkResult::Valid && Index < Helper.GetMaxIndex(); ++Index)
			{
				if (Helper.IsValidIndex(Index))
				{
					Result = WalkPayloadPropertyValue(
						*SetProperty->ElementProp, Helper.GetElementPtr(Index), Budget, Depth + 1,
						AllowedDynamicPayloadStructs, AllowedPayloadNames);
				}
			}
			return Result;
		}
		if (const FMapProperty* MapProperty = CastField<FMapProperty>(&Property))
		{
			FScriptMapHelper Helper(MapProperty, ValuePtr);
			EPayloadWalkResult Result = Budget.AddBytes(4);
			if (Result == EPayloadWalkResult::Valid)
			{
				Result = Budget.AddElements(static_cast<uint64>(Helper.Num()));
			}
			for (int32 Index = 0; Result == EPayloadWalkResult::Valid && Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}
				Result = WalkPayloadPropertyValue(
					*MapProperty->KeyProp, Helper.GetKeyPtr(Index), Budget, Depth + 1,
					AllowedDynamicPayloadStructs, AllowedPayloadNames);
				if (Result == EPayloadWalkResult::Valid)
				{
					Result = WalkPayloadPropertyValue(
						*MapProperty->ValueProp, Helper.GetValuePtr(Index), Budget, Depth + 1,
						AllowedDynamicPayloadStructs, AllowedPayloadNames);
				}
			}
			return Result;
		}
		if (const FOptionalProperty* OptionalProperty = CastField<FOptionalProperty>(&Property))
		{
			EPayloadWalkResult Result = Budget.AddBytes(1);
			const void* OptionalValue = OptionalProperty->GetValuePointerForReadIfSet(ValuePtr);
			if (Result == EPayloadWalkResult::Valid && OptionalValue)
			{
				Result = Budget.AddElements(1);
			}
			return Result == EPayloadWalkResult::Valid && OptionalValue
				? WalkPayloadPropertyValue(
					*OptionalProperty->GetValueProperty(), OptionalValue, Budget, Depth + 1,
					AllowedDynamicPayloadStructs, AllowedPayloadNames)
				: Result;
		}

		if (Property.IsA<FBoolProperty>())
		{
			return Budget.AddBytes(1);
		}
		if (Property.IsA<FByteProperty>()
			|| Property.IsA<FInt8Property>()
			|| Property.IsA<FInt16Property>()
			|| Property.IsA<FIntProperty>()
			|| Property.IsA<FInt64Property>()
			|| Property.IsA<FUInt16Property>()
			|| Property.IsA<FUInt32Property>()
			|| Property.IsA<FUInt64Property>()
			|| Property.IsA<FEnumProperty>())
		{
			const uint64 Width = CanonicalIntegerPropertyBytes(Property);
			return Width > 0
				? Budget.AddBytes(Width)
				: EPayloadWalkResult::UnsupportedField;
		}
		if (Property.IsA<FNameProperty>())
		{
			const FName Name = *static_cast<const FName*>(ValuePtr);
			if (!Name.IsNone() && !AllowedPayloadNames.Contains(Name))
			{
				return EPayloadWalkResult::NameOutsideCatalog;
			}
			return Budget.AddBytes(4);
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				const FInstancedStruct& Nested = *static_cast<const FInstancedStruct*>(ValuePtr);
				if (!Nested.IsValid())
				{
					return EPayloadWalkResult::UnsupportedField;
				}
				EPayloadWalkResult Result = Budget.AddElements(1);
				if (Result == EPayloadWalkResult::Valid)
				{
					Result = AddCanonicalStringBytes(Nested.GetScriptStruct()->GetPathName(), Budget);
				}
				if (Result == EPayloadWalkResult::Valid)
				{
					Result = ValidatePayloadStructType(Nested.GetScriptStruct());
				}
				if (Result == EPayloadWalkResult::Valid
					&& !AllowedDynamicPayloadStructs.Contains(Nested.GetScriptStruct()))
				{
					Result = EPayloadWalkResult::UnsupportedField;
				}
				return Result == EPayloadWalkResult::Valid
					? WalkPayloadStructValue(
						Nested.GetScriptStruct(), Nested.GetMemory(), Budget, Depth + 1,
						AllowedDynamicPayloadStructs, AllowedPayloadNames)
					: Result;
			}
			return WalkPayloadStructValue(
				StructProperty->Struct, ValuePtr, Budget, Depth + 1,
				AllowedDynamicPayloadStructs, AllowedPayloadNames);
		}

		return EPayloadWalkResult::UnsupportedField;
	}

	EPayloadWalkResult WalkPayloadStructValue(
		const UScriptStruct* Struct,
		const void* Memory,
		FPayloadBudget& Budget,
		int32 Depth,
		TConstArrayView<const UScriptStruct*> AllowedDynamicPayloadStructs,
		TConstArrayView<FName> AllowedPayloadNames)
	{
		if (!Struct || !Memory || Depth > 64)
		{
			return EPayloadWalkResult::TooLarge;
		}

		if (Struct == FGameplayTag::StaticStruct())
		{
			return AddCanonicalStringBytes(
				static_cast<const FGameplayTag*>(Memory)->ToString(), Budget);
		}
		if (Struct == FGameplayTagContainer::StaticStruct())
		{
			const TArray<FGameplayTag>& Tags =
				static_cast<const FGameplayTagContainer*>(Memory)->GetGameplayTagArray();
			EPayloadWalkResult Result = Budget.AddBytes(4);
			if (Result == EPayloadWalkResult::Valid)
			{
				Result = Budget.AddElements(static_cast<uint64>(Tags.Num()));
			}
			for (const FGameplayTag& Tag : Tags)
			{
				if (Result != EPayloadWalkResult::Valid)
				{
					break;
				}
				Result = AddCanonicalStringBytes(Tag.ToString(), Budget);
			}
			return Result;
		}

		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const FProperty& Property = **It;
			for (int32 ArrayIndex = 0; ArrayIndex < Property.ArrayDim; ++ArrayIndex)
			{
				const void* ValuePtr = Property.ContainerPtrToValuePtr<void>(Memory, ArrayIndex);
				const EPayloadWalkResult Result = WalkPayloadPropertyValue(
					Property, ValuePtr, Budget, Depth + 1,
					AllowedDynamicPayloadStructs, AllowedPayloadNames);
				if (Result != EPayloadWalkResult::Valid)
				{
					return Result;
				}
			}
		}
		return EPayloadWalkResult::Valid;
	}

	EPayloadWalkResult ValidatePayloadValue(
		const FInstancedStruct& Payload,
		int32 MaxBytes,
		int32 MaxElements,
		TConstArrayView<const UScriptStruct*> AllowedDynamicPayloadStructs,
		TConstArrayView<FName> AllowedPayloadNames)
	{
		const EPayloadWalkResult TypeResult = ValidatePayloadStructType(Payload.GetScriptStruct());
		if (TypeResult != EPayloadWalkResult::Valid)
		{
			return TypeResult;
		}
		FPayloadBudget Budget(MaxBytes, MaxElements);
		return WalkPayloadStructValue(
			Payload.GetScriptStruct(), Payload.GetMemory(), Budget, 0,
			AllowedDynamicPayloadStructs, AllowedPayloadNames);
	}

	ESeinCommandStructureResult ValidateCommandAgainstDescriptor(
		const FSeinCommand& Command,
		const FSeinCommandSchemaDescriptor& Descriptor,
		FSeinCommandSchemaDescriptor* OutDescriptor)
	{
		if (OutDescriptor)
		{
			*OutDescriptor = Descriptor;
		}

		if (!Descriptor.PayloadStruct)
		{
			if (Command.Payload.IsValid())
			{
				return ESeinCommandStructureResult::UnexpectedPayload;
			}
		}
		else
		{
			if (!Command.Payload.IsValid())
			{
				return ESeinCommandStructureResult::MissingPayload;
			}

			const UScriptStruct* ActualPayloadStruct = Command.Payload.GetScriptStruct();
			if (!FSeinCommandSchemaRegistry::IsDeterministicPayloadStruct(ActualPayloadStruct))
			{
				return ESeinCommandStructureResult::NonDeterministicPayload;
			}
			if (ActualPayloadStruct != Descriptor.PayloadStruct)
			{
				return ESeinCommandStructureResult::WrongPayloadType;
			}

			const EPayloadWalkResult PayloadResult = ValidatePayloadValue(
				Command.Payload,
				Descriptor.MaxPayloadBytes,
				Descriptor.MaxPayloadAggregateElements,
				Descriptor.DynamicPayloadStructs,
				Descriptor.AllowedPayloadNames);
			switch (PayloadResult)
			{
			case EPayloadWalkResult::Valid:
				break;
			case EPayloadWalkResult::NonDeterministic:
				return ESeinCommandStructureResult::NonDeterministicPayload;
			case EPayloadWalkResult::UnsupportedField:
				return ESeinCommandStructureResult::UnsupportedPayloadField;
			case EPayloadWalkResult::NameOutsideCatalog:
				return ESeinCommandStructureResult::PayloadNameOutsideCatalog;
			case EPayloadWalkResult::TooLarge:
				return ESeinCommandStructureResult::PayloadTooLarge;
			default:
				return ESeinCommandStructureResult::UnsupportedPayloadField;
			}
		}

		if (Command.EntityList.Num() > Descriptor.MaxEntityListEntries)
		{
			return ESeinCommandStructureResult::EntityListTooLarge;
		}
		if (Command.TargeterPoints.Num() > Descriptor.MaxTargeterPoints)
		{
			return ESeinCommandStructureResult::TargeterPointsTooLarge;
		}
		return ESeinCommandStructureResult::Valid;
	}

	bool CanonicalizePayloadNameProperty(
		const FProperty& Property,
		void* ValuePtr,
		TConstArrayView<FName> AllowedPayloadNames,
		int32 Depth);

	bool CanonicalizePayloadNameStruct(
		const UScriptStruct* Struct,
		void* Memory,
		TConstArrayView<FName> AllowedPayloadNames,
		int32 Depth)
	{
		if (!Struct || !Memory || Depth > 64)
		{
			return false;
		}
		if (Struct == FGameplayTag::StaticStruct()
			|| Struct == FGameplayTagContainer::StaticStruct())
		{
			return true;
		}
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const FProperty& Property = **It;
			for (int32 ArrayIndex = 0; ArrayIndex < Property.ArrayDim; ++ArrayIndex)
			{
				if (!CanonicalizePayloadNameProperty(
					Property,
					Property.ContainerPtrToValuePtr<void>(Memory, ArrayIndex),
					AllowedPayloadNames,
					Depth + 1))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool CanonicalizePayloadNameProperty(
		const FProperty& Property,
		void* ValuePtr,
		TConstArrayView<FName> AllowedPayloadNames,
		int32 Depth)
	{
		if (!ValuePtr || Depth > 64)
		{
			return false;
		}
		if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
		{
			FScriptArrayHelper Helper(Array, ValuePtr);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				if (!CanonicalizePayloadNameProperty(
					*Array->Inner, Helper.GetRawPtr(Index), AllowedPayloadNames, Depth + 1))
				{
					return false;
				}
			}
			return true;
		}
		if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(&Property))
		{
			const void* OptionalValue = Optional->GetValuePointerForReadIfSet(ValuePtr);
			return !OptionalValue || CanonicalizePayloadNameProperty(
				*Optional->GetValueProperty(), const_cast<void*>(OptionalValue),
				AllowedPayloadNames, Depth + 1);
		}
		if (Property.IsA<FNameProperty>())
		{
			FName& Name = *static_cast<FName*>(ValuePtr);
			if (Name.IsNone())
			{
				return true;
			}
			for (const FName Canonical : AllowedPayloadNames)
			{
				if (Canonical == Name)
				{
					Name = Canonical;
					return true;
				}
			}
			return false;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				FInstancedStruct& Dynamic = *static_cast<FInstancedStruct*>(ValuePtr);
				return Dynamic.IsValid() && CanonicalizePayloadNameStruct(
					Dynamic.GetScriptStruct(), Dynamic.GetMutableMemory(),
					AllowedPayloadNames, Depth + 1);
			}
			return CanonicalizePayloadNameStruct(
				StructProperty->Struct, ValuePtr, AllowedPayloadNames, Depth + 1);
		}
		return true;
	}
}

void SeinBuildCanonicalWireNameCatalog(
	TConstArrayView<FName> Names,
	TArray<FName>& OutCanonicalNames,
	FString& OutCanonicalManifest)
{
	BuildCanonicalWireNameCatalogImpl(
		Names, OutCanonicalNames, OutCanonicalManifest);
}

ESeinCommandStructureResult SeinValidateCommandAgainstSchema(
	const FSeinCommand& Command,
	const FSeinCommandSchemaDescriptor& Descriptor)
{
	if (!Command.CommandType.IsValid())
	{
		return ESeinCommandStructureResult::InvalidCommandType;
	}
	if (Command.SchemaVersion <= 0)
	{
		return ESeinCommandStructureResult::InvalidSchemaVersion;
	}
	if (Command.CommandType != Descriptor.CommandType)
	{
		return ESeinCommandStructureResult::UnknownCommandType;
	}
	if (Command.SchemaVersion != Descriptor.SchemaVersion)
	{
		return ESeinCommandStructureResult::UnsupportedSchemaVersion;
	}
	return ValidateCommandAgainstDescriptor(Command, Descriptor, nullptr);
}

bool SeinCanonicalizeCommandPayloadNames(
	FSeinCommand& Command,
	const FSeinCommandSchemaDescriptor& Descriptor)
{
	return !Command.Payload.IsValid()
		|| CanonicalizePayloadNameStruct(
			Command.Payload.GetScriptStruct(), Command.Payload.GetMutableMemory(),
			Descriptor.AllowedPayloadNames, 0);
}

struct FSeinCommandSchemaSnapshot::FData
{
	struct FKey
	{
		FGameplayTag CommandType;
		int32 SchemaVersion = 0;

		bool operator==(const FKey& Other) const
		{
			return CommandType == Other.CommandType && SchemaVersion == Other.SchemaVersion;
		}

		friend uint32 GetTypeHash(const FKey& Key)
		{
			return HashCombine(GetTypeHash(Key.CommandType), GetTypeHash(Key.SchemaVersion));
		}
	};

	TMap<FKey, FSeinCommandSchemaDescriptor> Schemas;
	TSet<FGameplayTag> KnownCommandTypes;
	TArray<TStrongObjectPtr<UScriptStruct>> PayloadStructRoots;
	TArray<TStrongObjectPtr<UScriptStruct>> DynamicPayloadStructRoots;
	TArray<TStrongObjectPtr<UClass>> HandlerClassRoots;
	TArray<const UScriptStruct*> AdditionalDynamicPayloadStructs;
	TArray<FName> AdditionalWireNames;
	FString CanonicalManifest;
	FGuid CanonicalManifestDigest;
};

int32 FSeinCommandSchemaSnapshot::GetSchemaCount() const
{
	return Data ? Data->Schemas.Num() : 0;
}

bool FSeinCommandSchemaSnapshot::FindSchema(
	FGameplayTag CommandType,
	int32 SchemaVersion,
	FSeinCommandSchemaDescriptor& OutDescriptor) const
{
	if (!Data)
	{
		return false;
	}

	const FData::FKey Key{ CommandType, SchemaVersion };
	const FSeinCommandSchemaDescriptor* Descriptor = Data->Schemas.Find(Key);
	if (!Descriptor)
	{
		return false;
	}
	OutDescriptor = *Descriptor;
	return true;
}

ESeinCommandStructureResult FSeinCommandSchemaSnapshot::ValidateStructure(
	const FSeinCommand& Command,
	FSeinCommandSchemaDescriptor* OutDescriptor) const
{
	if (OutDescriptor)
	{
		*OutDescriptor = {};
	}
	if (!Command.CommandType.IsValid())
	{
		return ESeinCommandStructureResult::InvalidCommandType;
	}
	if (Command.SchemaVersion <= 0)
	{
		return ESeinCommandStructureResult::InvalidSchemaVersion;
	}

	FSeinCommandSchemaDescriptor Descriptor;
	if (!FindSchema(Command.CommandType, Command.SchemaVersion, Descriptor))
	{
		return Data && Data->KnownCommandTypes.Contains(Command.CommandType)
			? ESeinCommandStructureResult::UnsupportedSchemaVersion
			: ESeinCommandStructureResult::UnknownCommandType;
	}
	return ValidateCommandAgainstDescriptor(Command, Descriptor, OutDescriptor);
}

const FString& FSeinCommandSchemaSnapshot::GetCanonicalManifest() const
{
	static const FString EmptyManifest;
	return Data ? Data->CanonicalManifest : EmptyManifest;
}

FGuid FSeinCommandSchemaSnapshot::GetCanonicalManifestDigest() const
{
	return Data ? Data->CanonicalManifestDigest : FGuid();
}

TConstArrayView<const UScriptStruct*>
FSeinCommandSchemaSnapshot::GetAdditionalDynamicPayloadStructs() const
{
	return Data ? TConstArrayView<const UScriptStruct*>(Data->AdditionalDynamicPayloadStructs) : TConstArrayView<const UScriptStruct*>();
}

TConstArrayView<FName> FSeinCommandSchemaSnapshot::GetAdditionalWireNames() const
{
	return Data ? TConstArrayView<FName>(Data->AdditionalWireNames) : TConstArrayView<FName>();
}

bool USeinCommandHandler::ExecuteCommand_Implementation(
	USeinWorldSubsystem* World,
	const FSeinCommand& Command,
	FGameplayTag& OutRejectionReason) const
{
	OutRejectionReason = FGameplayTag();
	return false;
}

FSeinCommandSchemaDescriptor USeinCommandHandler::BuildSchemaDescriptor() const
{
	FSeinCommandSchemaDescriptor Descriptor;
	Descriptor.StableSchemaId = StableSchemaId;
	Descriptor.CommandType = CommandType;
	Descriptor.SchemaVersion = SchemaVersion;
	Descriptor.ImplementationRevision = ImplementationRevision;
	Descriptor.PayloadStruct = PayloadSchema.GetScriptStruct();
	Descriptor.DynamicPayloadStructs.Reserve(DynamicPayloadSchemas.Num());
	for (const FInstancedStruct& Schema : DynamicPayloadSchemas)
	{
		if (Schema.IsValid())
		{
			Descriptor.DynamicPayloadStructs.Add(Schema.GetScriptStruct());
		}
	}
	Descriptor.AllowedPayloadNames = AllowedPayloadNames;
	Descriptor.AuthorityScope = AuthorityScope;
	Descriptor.MaxEntityListEntries = MaxEntityListEntries;
	Descriptor.MaxTargeterPoints = MaxTargeterPoints;
	Descriptor.MaxPayloadBytes = MaxPayloadBytes;
	Descriptor.MaxPayloadAggregateElements = MaxPayloadAggregateElements;
	Descriptor.AllowedExecutionContexts = AllowedExecutionContexts;
	Descriptor.HandlerClass = GetClass();
	return Descriptor;
}

FSeinCommandSchemaRegistrationHandle FSeinCommandSchemaRegistry::RegisterHandlerClass(
	FName OwnerId,
	TSubclassOf<USeinCommandHandler> HandlerClass)
{
	const UClass* Class = HandlerClass.Get();
	const USeinCommandHandler* HandlerCDO = Class
		? Cast<USeinCommandHandler>(Class->GetDefaultObject())
		: nullptr;
	if (!HandlerCDO)
	{
		UE_LOG(LogSeinCommandSchema, Error,
			TEXT("Rejected command handler registration for owner '%s': missing handler CDO."),
			*OwnerId.ToString());
		return {};
	}
	return RegisterSchema(OwnerId, HandlerCDO->BuildSchemaDescriptor());
}

FSeinCommandSchemaRegistrationHandle FSeinCommandSchemaRegistry::RegisterSchema(
	FName OwnerId,
	const FSeinCommandSchemaDescriptor& Descriptor)
{
	const UClass* HandlerClass = Descriptor.HandlerClass.Get();
	if (OwnerId.IsNone() || Descriptor.StableSchemaId.IsNone()
		|| !Descriptor.CommandType.IsValid() || Descriptor.SchemaVersion <= 0
		|| Descriptor.ImplementationRevision <= 0
		|| !IsKnownAuthorityScope(Descriptor.AuthorityScope)
		|| Descriptor.MaxEntityListEntries < 0 || Descriptor.MaxTargeterPoints < 0
		|| Descriptor.MaxPayloadBytes < 0 || Descriptor.MaxPayloadAggregateElements < 0
		|| (Descriptor.AllowedExecutionContexts & ~KnownExecutionAllowanceMask) != 0
		|| !HandlerClass || !HandlerClass->IsChildOf(USeinCommandHandler::StaticClass())
		|| HandlerClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		|| !HandlerClass->HasAnyClassFlags(CLASS_Const))
	{
		UE_LOG(LogSeinCommandSchema, Error,
			TEXT("Rejected command schema '%s': expected non-empty owner/schema IDs, a valid tag, positive wire/implementation revisions, known authority scope, non-negative budgets, known execution flags, and a concrete const handler class."),
			*Descriptor.StableSchemaId.ToString());
		return {};
	}

	FString PayloadLayoutManifest = TEXT("<none>");
	if (Descriptor.PayloadStruct)
	{
		const EPayloadWalkResult PayloadTypeResult = ValidatePayloadStructType(Descriptor.PayloadStruct);
		if (PayloadTypeResult != EPayloadWalkResult::Valid
			|| !BuildPayloadStructLayout(Descriptor.PayloadStruct, PayloadLayoutManifest))
		{
			const TCHAR* Reason = PayloadTypeResult == EPayloadWalkResult::NonDeterministic
				? TEXT("is not SeinDeterministic")
				: TEXT("contains an unsupported or non-serialized field type");
			UE_LOG(LogSeinCommandSchema, Error,
				TEXT("Rejected command schema '%s': payload struct '%s' %s."),
				*Descriptor.StableSchemaId.ToString(),
				*Descriptor.PayloadStruct->GetPathName(), Reason);
			return {};
		}
	}

	FString DynamicPayloadManifest;
	TArray<const UScriptStruct*> CanonicalDynamicPayloadStructs;
	if (!BuildDynamicPayloadManifest(
		Descriptor.DynamicPayloadStructs,
		DynamicPayloadManifest,
		CanonicalDynamicPayloadStructs))
	{
		UE_LOG(LogSeinCommandSchema, Error,
			TEXT("Rejected command schema '%s': a dynamic payload schema is null, unsupported, non-deterministic, or conflicts by stable type path."),
			*Descriptor.StableSchemaId.ToString());
		return {};
	}
	FString AllowedPayloadNameManifest;
	TArray<FName> CanonicalAllowedPayloadNames;
	BuildCanonicalWireNameCatalogImpl(
		Descriptor.AllowedPayloadNames,
		CanonicalAllowedPayloadNames,
		AllowedPayloadNameManifest);

	FScopeLock Lock(&GetRegistryMutex());
	TArray<FRegisteredCommandSchema>& Registry = GetRegistry();
	for (FRegisteredCommandSchema& Registered : Registry)
	{
		if (HasExactKey(Registered, Descriptor.CommandType, Descriptor.SchemaVersion))
		{
			if (!IsExactDuplicate(
				Registered,
				OwnerId,
				Descriptor,
				PayloadLayoutManifest,
				DynamicPayloadManifest,
				AllowedPayloadNameManifest))
			{
				UE_LOG(LogSeinCommandSchema, Error,
					TEXT("Rejected conflicting command schema key '%s' version %d (existing schema '%s', requested '%s')."),
					*Descriptor.CommandType.ToString(), Descriptor.SchemaVersion,
					*Registered.StableSchemaId.ToString(), *Descriptor.StableSchemaId.ToString());
				return {};
			}

			const uint64 Token = AllocateRegistrationToken();
			Registered.RegistrationClaims.Emplace(
				Token,
				const_cast<UScriptStruct*>(Descriptor.PayloadStruct),
				Descriptor.HandlerClass.Get(),
				CanonicalDynamicPayloadStructs);
			FSeinCommandSchemaRegistrationHandle Handle;
			Handle.OwnerId = OwnerId;
			Handle.Token = Token;
			return Handle;
		}

		if (Registered.StableSchemaId == Descriptor.StableSchemaId)
		{
			UE_LOG(LogSeinCommandSchema, Error,
				TEXT("Rejected conflicting command StableSchemaId '%s' (existing key '%s' version %d, requested key '%s' version %d)."),
				*Descriptor.StableSchemaId.ToString(),
				*Registered.CommandType.ToString(), Registered.SchemaVersion,
				*Descriptor.CommandType.ToString(), Descriptor.SchemaVersion);
			return {};
		}
	}

	const uint64 Token = AllocateRegistrationToken();
	FRegisteredCommandSchema Registered;
	Registered.OwnerId = OwnerId;
	Registered.StableSchemaId = Descriptor.StableSchemaId;
	Registered.CommandType = Descriptor.CommandType;
	Registered.SchemaVersion = Descriptor.SchemaVersion;
	Registered.ImplementationRevision = Descriptor.ImplementationRevision;
	Registered.PayloadStructPath = GetPayloadStructPath(Descriptor.PayloadStruct);
	Registered.PayloadLayoutManifest = MoveTemp(PayloadLayoutManifest);
	Registered.DynamicPayloadManifest = MoveTemp(DynamicPayloadManifest);
	Registered.AllowedPayloadNames = MoveTemp(CanonicalAllowedPayloadNames);
	Registered.AllowedPayloadNameManifest = MoveTemp(AllowedPayloadNameManifest);
	Registered.AuthorityScope = Descriptor.AuthorityScope;
	Registered.MaxEntityListEntries = Descriptor.MaxEntityListEntries;
	Registered.MaxTargeterPoints = Descriptor.MaxTargeterPoints;
	Registered.MaxPayloadBytes = Descriptor.MaxPayloadBytes;
	Registered.MaxPayloadAggregateElements = Descriptor.MaxPayloadAggregateElements;
	Registered.AllowedExecutionContexts = Descriptor.AllowedExecutionContexts;
	Registered.HandlerClassPath = GetHandlerClassPath(Descriptor.HandlerClass);
	Registered.RegistrationClaims.Emplace(
		Token,
		const_cast<UScriptStruct*>(Descriptor.PayloadStruct),
		Descriptor.HandlerClass.Get(),
		CanonicalDynamicPayloadStructs);
	Registry.Add(MoveTemp(Registered));
	FSeinCommandSchemaRegistrationHandle Handle;
	Handle.OwnerId = OwnerId;
	Handle.Token = Token;
	return Handle;
}

bool FSeinCommandSchemaRegistry::UnregisterSchema(
	FSeinCommandSchemaRegistrationHandle& Handle)
{
	if (!Handle.IsValid())
	{
		return false;
	}

	const FName OwnerId = Handle.OwnerId;
	const uint64 Token = Handle.Token;
	Handle.Reset();

	FScopeLock Lock(&GetRegistryMutex());
	TArray<FRegisteredCommandSchema>& Registry = GetRegistry();
	for (int32 Index = 0; Index < Registry.Num(); ++Index)
	{
		FRegisteredCommandSchema& Registered = Registry[Index];
		if (Registered.OwnerId != OwnerId)
		{
			continue;
		}

		const int32 ClaimIndex = Registered.RegistrationClaims.IndexOfByPredicate(
			[Token](const FCommandSchemaRegistrationClaim& Claim)
			{
				return Claim.Token == Token;
			});
		if (ClaimIndex == INDEX_NONE)
		{
			continue;
		}

		Registered.RegistrationClaims.RemoveAt(ClaimIndex);
		if (Registered.RegistrationClaims.IsEmpty())
		{
			Registry.RemoveAt(Index);
		}
		return true;
	}
	return false;
}

bool FSeinCommandSchemaRegistry::FindSchema(
	FGameplayTag CommandType,
	int32 SchemaVersion,
	FSeinCommandSchemaDescriptor& OutDescriptor)
{
	FScopeLock Lock(&GetRegistryMutex());
	const FRegisteredCommandSchema* Registered = GetRegistry().FindByPredicate(
		[&](const FRegisteredCommandSchema& Candidate)
		{
			return HasExactKey(Candidate, CommandType, SchemaVersion);
		});
	if (!Registered)
	{
		return false;
	}

	CopyDescriptor(*Registered, OutDescriptor);
	return true;
}

ESeinCommandStructureResult FSeinCommandSchemaRegistry::ValidateStructure(
	const FSeinCommand& Command,
	FSeinCommandSchemaDescriptor* OutDescriptor)
{
	if (OutDescriptor)
	{
		*OutDescriptor = {};
	}
	if (!Command.CommandType.IsValid())
	{
		return ESeinCommandStructureResult::InvalidCommandType;
	}
	if (Command.SchemaVersion <= 0)
	{
		return ESeinCommandStructureResult::InvalidSchemaVersion;
	}

	FSeinCommandSchemaDescriptor Descriptor;
	bool bKnownCommandType = false;
	{
		FScopeLock Lock(&GetRegistryMutex());
		for (const FRegisteredCommandSchema& Registered : GetRegistry())
		{
			if (Registered.CommandType != Command.CommandType)
			{
				continue;
			}

			bKnownCommandType = true;
			if (Registered.SchemaVersion == Command.SchemaVersion)
			{
				CopyDescriptor(Registered, Descriptor);
				break;
			}
		}
	}

	if (Descriptor.StableSchemaId.IsNone())
	{
		return bKnownCommandType
			? ESeinCommandStructureResult::UnsupportedSchemaVersion
			: ESeinCommandStructureResult::UnknownCommandType;
	}
	return ValidateCommandAgainstDescriptor(Command, Descriptor, OutDescriptor);
}

FString FSeinCommandSchemaRegistry::BuildCanonicalManifest()
{
	TArray<FCanonicalCommandSchema> Schemas;
	{
		FScopeLock Lock(&GetRegistryMutex());
		Schemas.Reserve(GetRegistry().Num());
		for (const FRegisteredCommandSchema& Registered : GetRegistry())
		{
			Schemas.Add(MakeCanonicalSchema(Registered));
		}
	}
	return BuildCanonicalManifestFromSchemas(MoveTemp(Schemas));
}

FGuid FSeinCommandSchemaRegistry::ComputeCanonicalManifestDigest()
{
	return ComputeManifestDigest(BuildCanonicalManifest());
}

FSeinCommandSchemaSnapshot FSeinCommandSchemaRegistry::CaptureSnapshot(
	TConstArrayView<const UScriptStruct*> AdditionalDynamicPayloadStructs,
	TConstArrayView<FName> AdditionalWireNames)
{
	FString AdditionalDynamicPayloadManifest;
	TArray<const UScriptStruct*> CanonicalAdditionalDynamicPayloadStructs;
	if (!BuildDynamicPayloadManifest(
		AdditionalDynamicPayloadStructs,
		AdditionalDynamicPayloadManifest,
		CanonicalAdditionalDynamicPayloadStructs))
	{
		UE_LOG(LogSeinCommandSchema, Error,
			TEXT("Failed to capture command schemas: an additional dynamic payload type is invalid or conflicts by identity/path."));
		return {};
	}
	FString AdditionalWireNameManifest;
	TArray<FName> CanonicalAdditionalWireNames;
	BuildCanonicalWireNameCatalogImpl(
		AdditionalWireNames,
		CanonicalAdditionalWireNames,
		AdditionalWireNameManifest);

	TSharedRef<FSeinCommandSchemaSnapshot::FData, ESPMode::ThreadSafe> SnapshotData =
		MakeShared<FSeinCommandSchemaSnapshot::FData, ESPMode::ThreadSafe>();
	TArray<FCanonicalCommandSchema> CanonicalSchemas;
	SnapshotData->AdditionalDynamicPayloadStructs =
		CanonicalAdditionalDynamicPayloadStructs;
	SnapshotData->AdditionalWireNames = CanonicalAdditionalWireNames;
	{
		FScopeLock Lock(&GetRegistryMutex());
		const TArray<FRegisteredCommandSchema>& Registry = GetRegistry();
		SnapshotData->Schemas.Reserve(Registry.Num());
		SnapshotData->KnownCommandTypes.Reserve(Registry.Num());
		SnapshotData->PayloadStructRoots.Reserve(Registry.Num());
		SnapshotData->DynamicPayloadStructRoots.Reserve(
			Registry.Num() + CanonicalAdditionalDynamicPayloadStructs.Num());
		SnapshotData->HandlerClassRoots.Reserve(Registry.Num());
		CanonicalSchemas.Reserve(Registry.Num());

		for (const FRegisteredCommandSchema& Registered : Registry)
		{
			FSeinCommandSchemaDescriptor Descriptor;
			CopyDescriptor(Registered, Descriptor);
			TArray<const UScriptStruct*> CombinedDynamicPayloadStructs =
				Descriptor.DynamicPayloadStructs;
			CombinedDynamicPayloadStructs.Append(
				CanonicalAdditionalDynamicPayloadStructs);
			FString CombinedDynamicManifest;
			if (!BuildDynamicPayloadManifest(
				CombinedDynamicPayloadStructs,
				CombinedDynamicManifest,
				Descriptor.DynamicPayloadStructs))
			{
				UE_LOG(LogSeinCommandSchema, Error,
					TEXT("Failed to capture command schema '%s': dynamic payload generations conflict."),
					*Descriptor.StableSchemaId.ToString());
				return {};
			}
			TArray<FName> CombinedAllowedPayloadNames = Descriptor.AllowedPayloadNames;
			CombinedAllowedPayloadNames.Append(CanonicalAdditionalWireNames);
			FString IgnoredCombinedPayloadNameManifest;
			BuildCanonicalWireNameCatalogImpl(
				CombinedAllowedPayloadNames,
				Descriptor.AllowedPayloadNames,
				IgnoredCombinedPayloadNameManifest);
			const FSeinCommandSchemaSnapshot::FData::FKey Key{
				Descriptor.CommandType,
				Descriptor.SchemaVersion
			};
			SnapshotData->Schemas.Add(Key, Descriptor);
			SnapshotData->KnownCommandTypes.Add(Descriptor.CommandType);
			if (Descriptor.PayloadStruct)
			{
				SnapshotData->PayloadStructRoots.Emplace(
					const_cast<UScriptStruct*>(Descriptor.PayloadStruct));
			}
			for (const UScriptStruct* DynamicStruct : Descriptor.DynamicPayloadStructs)
			{
				SnapshotData->DynamicPayloadStructRoots.Emplace(
					const_cast<UScriptStruct*>(DynamicStruct));
			}
			SnapshotData->HandlerClassRoots.Emplace(Descriptor.HandlerClass.Get());
			CanonicalSchemas.Add(MakeCanonicalSchema(Registered));
		}
	}

	SnapshotData->CanonicalManifest =
		BuildCanonicalManifestFromSchemas(
			MoveTemp(CanonicalSchemas),
			AdditionalDynamicPayloadManifest,
			AdditionalWireNameManifest);
	SnapshotData->CanonicalManifestDigest =
		ComputeManifestDigest(SnapshotData->CanonicalManifest);

	FSeinCommandSchemaSnapshot Snapshot;
	Snapshot.Data = SnapshotData;
	return Snapshot;
}

int32 FSeinCommandSchemaRegistry::GetRegisteredSchemaCount()
{
	FScopeLock Lock(&GetRegistryMutex());
	return GetRegistry().Num();
}

bool FSeinCommandSchemaRegistry::IsDeterministicPayloadStruct(
	const UScriptStruct* PayloadStruct)
{
	if (!PayloadStruct)
	{
		return false;
	}
#if WITH_METADATA
	static const FName SeinDeterministicMeta(TEXT("SeinDeterministic"));
	return PayloadStruct->HasMetaData(SeinDeterministicMeta);
#else
	// Custom UField metadata is stripped from cooked builds. Registration is a
	// trusted module/asset-load operation; frozen exact UObject identity plus the
	// recursive layout manifest prevents arbitrary/reloaded types reaching handlers.
	return true;
#endif
}
