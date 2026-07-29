/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDeterministicValueDigest.cpp
 */

#include "Serialization/SeinDeterministicValueDigest.h"

#include "GameplayTagContainer.h"
#include "Hash/Blake3.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/PropertyOptional.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr uint16 CanonicalFormatVersion = 1;
	constexpr uint64 MaxTArrayBytes = static_cast<uint64>(MAX_int32);

	enum class EValueToken : uint8
	{
		Struct = 1,
		Property,
		Bool,
		Integer,
		Enum,
		Name,
		String,
		GameplayTag,
		GameplayTagContainer,
		Array,
		Set,
		Map,
		Optional,
		InstancedStruct,
	};

	struct FEncodeContext
	{
		explicit FEncodeContext(const FSeinDeterministicValueDigestOptions& InOptions)
			: Options(InOptions)
		{
		}

		bool Fail(
			ESeinDeterministicValueDigestResult Result,
			const FString& FieldPath,
			const FString& Message,
			bool bReplace = false)
		{
			if (bReplace || Error.Result == ESeinDeterministicValueDigestResult::Success)
			{
				Error.Result = Result;
				Error.FieldPath = FieldPath;
				Error.Message = Message;
			}
			return false;
		}

		bool CheckDepth(int32 Depth)
		{
			return Depth <= Options.MaxRecursionDepth
				|| Fail(
					ESeinDeterministicValueDigestResult::RecursionLimitExceeded,
					TEXT("$depth"),
					TEXT("Canonical value recursion limit exceeded."));
		}

		bool AddElements(uint64 Count)
		{
			if (Count > Options.MaxAggregateElements
				|| AggregateElements > Options.MaxAggregateElements - Count)
			{
				return Fail(
					ESeinDeterministicValueDigestResult::ElementLimitExceeded,
					TEXT("$elements"),
					TEXT("Canonical value aggregate element limit exceeded."));
			}
			AggregateElements += Count;
			return true;
		}

		bool Append(TArray<uint8>& Out, const void* Bytes, uint64 NumBytes)
		{
			const uint64 Existing = static_cast<uint64>(Out.Num());
			if (NumBytes > Options.MaxEncodedBytes
				|| Existing > Options.MaxEncodedBytes - NumBytes
				|| Existing > MaxTArrayBytes - NumBytes)
			{
				return Fail(
					ESeinDeterministicValueDigestResult::ByteLimitExceeded,
					TEXT("$bytes"),
					TEXT("Canonical value byte limit exceeded."));
			}
			if (NumBytes > 0)
			{
				Out.Append(static_cast<const uint8*>(Bytes), static_cast<int32>(NumBytes));
			}
			return true;
		}

		const FSeinDeterministicValueDigestOptions& Options;
		uint64 AggregateElements = 0;
		FSeinDeterministicValueDigestError Error;
	};

	bool AppendByte(FEncodeContext& Context, TArray<uint8>& Out, uint8 Value)
	{
		return Context.Append(Out, &Value, sizeof(Value));
	}

	bool AppendUIntBigEndian(
		FEncodeContext& Context,
		TArray<uint8>& Out,
		uint64 Value,
		int32 Width)
	{
		uint8 Bytes[8];
		if (Width != 1 && Width != 2 && Width != 4 && Width != 8)
		{
			return Context.Fail(
				ESeinDeterministicValueDigestResult::UnsupportedProperty,
				TEXT("$integer"),
				TEXT("Unsupported reflected integer width."));
		}
		for (int32 Index = 0; Index < Width; ++Index)
		{
			Bytes[Index] = static_cast<uint8>(Value >> ((Width - 1 - Index) * 8));
		}
		return Context.Append(Out, Bytes, static_cast<uint64>(Width));
	}

	bool AppendUInt16(FEncodeContext& Context, TArray<uint8>& Out, uint16 Value)
	{
		return AppendUIntBigEndian(Context, Out, Value, sizeof(Value));
	}

	bool AppendUInt64(FEncodeContext& Context, TArray<uint8>& Out, uint64 Value)
	{
		return AppendUIntBigEndian(Context, Out, Value, sizeof(Value));
	}

	bool AppendFramedBytes(
		FEncodeContext& Context,
		TArray<uint8>& Out,
		const TArray<uint8>& Bytes)
	{
		return AppendUInt64(Context, Out, static_cast<uint64>(Bytes.Num()))
			&& Context.Append(Out, Bytes.GetData(), static_cast<uint64>(Bytes.Num()));
	}

	bool AppendUtf8(
		FEncodeContext& Context,
		TArray<uint8>& Out,
		const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value, Value.Len());
		return AppendUInt64(Context, Out, static_cast<uint64>(Utf8.Length()))
			&& Context.Append(Out, Utf8.Get(), static_cast<uint64>(Utf8.Length()));
	}

	FString CanonicalNameString(const FName Name)
	{
		FString Result = Name.ToString();
		// Framework identifiers are ASCII. Folding explicitly, instead of using a
		// locale-sensitive transform, removes FName display-casing/load-order noise.
		for (TCHAR& Character : Result)
		{
			if (Character >= TCHAR('A') && Character <= TCHAR('Z'))
			{
				Character += TCHAR('a') - TCHAR('A');
			}
		}
		return Result;
	}

	bool BytesLess(const TArray<uint8>& A, const TArray<uint8>& B)
	{
		const int32 Shared = A.Num() < B.Num() ? A.Num() : B.Num();
		if (Shared > 0)
		{
			const int32 Comparison = FMemory::Memcmp(A.GetData(), B.GetData(), Shared);
			if (Comparison != 0)
			{
				return Comparison < 0;
			}
		}
		return A.Num() < B.Num();
	}

	bool ErrorLess(
		const FSeinDeterministicValueDigestError& A,
		const FSeinDeterministicValueDigestError& B)
	{
		if (A.Result != B.Result)
		{
			return static_cast<uint8>(A.Result) < static_cast<uint8>(B.Result);
		}
		const int32 PathOrder = A.FieldPath.Compare(B.FieldPath, ESearchCase::CaseSensitive);
		return PathOrder != 0
			? PathOrder < 0
			: A.Message.Compare(B.Message, ESearchCase::CaseSensitive) < 0;
	}

	void ConsiderError(
		TOptional<FSeinDeterministicValueDigestError>& Best,
		const FSeinDeterministicValueDigestError& Candidate)
	{
		if (!Best.IsSet() || ErrorLess(Candidate, Best.GetValue()))
		{
			Best = Candidate;
		}
	}

	void ConsiderError(
		TOptional<FSeinDeterministicValueDigestError>& Best,
		ESeinDeterministicValueDigestResult Result,
		const TCHAR* Path,
		const TCHAR* Message)
	{
		FSeinDeterministicValueDigestError Candidate;
		Candidate.Result = Result;
		Candidate.FieldPath = Path;
		Candidate.Message = Message;
		ConsiderError(Best, Candidate);
	}

	bool ApplyError(
		FEncodeContext& Context,
		const TOptional<FSeinDeterministicValueDigestError>& Error)
	{
		return !Error.IsSet() || Context.Fail(
			Error->Result, Error->FieldPath, Error->Message, true);
	}

	bool IsSpecialDeterministicStruct(const UScriptStruct* Struct)
	{
		return Struct == FGameplayTag::StaticStruct()
			|| Struct == FGameplayTagContainer::StaticStruct();
	}

	bool IsMarkedDeterministic(
		const UScriptStruct* Struct,
		const FSeinDeterministicValueDigestOptions& Options)
	{
		if (!Struct)
		{
			return false;
		}
		if (IsSpecialDeterministicStruct(Struct))
		{
			return true;
		}
#if WITH_METADATA
		(void)Options;
		static const FName DeterministicMeta(TEXT("SeinDeterministic"));
		return Struct->HasMetaData(DeterministicMeta);
#else
		// This is intentionally opt-in: the caller must first freeze and compare
		// its concrete type-path manifest. Merely being present in a cooked image
		// is not sufficient authority to enter deterministic state.
		return Options.bTrustCookedTypesWithoutMetadata;
#endif
	}

	FString PropertyOwnerPath(const FProperty& Property)
	{
		const UStruct* Owner = Property.GetOwnerStruct();
		return Owner ? Owner->GetPathName() : FString(TEXT("<none>"));
	}

	FString PropertyFieldPath(const FString& Parent, const FProperty& Property)
	{
		return Parent + TEXT(".") + Property.GetName();
	}

	bool NumericTypeName(const FNumericProperty& Property, FString& OutType)
	{
		if (Property.IsA<FByteProperty>())
		{
			OutType = TEXT("UInt8");
		}
		else if (Property.IsA<FInt8Property>())
		{
			OutType = TEXT("Int8");
		}
		else if (Property.IsA<FInt16Property>())
		{
			OutType = TEXT("Int16");
		}
		else if (Property.IsA<FUInt16Property>())
		{
			OutType = TEXT("UInt16");
		}
		else if (Property.IsA<FIntProperty>())
		{
			OutType = TEXT("Int32");
		}
		else if (Property.IsA<FUInt32Property>())
		{
			OutType = TEXT("UInt32");
		}
		else if (Property.IsA<FInt64Property>())
		{
			OutType = TEXT("Int64");
		}
		else if (Property.IsA<FUInt64Property>())
		{
			OutType = TEXT("UInt64");
		}
		else
		{
			return false;
		}
		if (const UEnum* Enum = Property.GetIntPropertyEnum())
		{
			OutType = FString::Printf(TEXT("ByteEnum(%s,%s)"),
				*Enum->GetPathName(), *OutType);
		}
		return true;
	}

	bool BuildPropertyTypeDescriptor(
		const FProperty& Property,
		FString& OutType,
		FEncodeContext& Context,
		const FString& FieldPath)
	{
		if (Property.HasAnyPropertyFlags(
			CPF_Transient | CPF_EditorOnly | CPF_Deprecated | CPF_SkipSerialization))
		{
			return Context.Fail(
				ESeinDeterministicValueDigestResult::UnsupportedProperty,
				FieldPath,
				TEXT("Transient, editor-only, deprecated, and skipped fields are not canonical state."));
		}

		if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
		{
			FString Inner;
			if (!BuildPropertyTypeDescriptor(*Array->Inner, Inner, Context, FieldPath + TEXT("[]")))
			{
				return false;
			}
			OutType = FString::Printf(TEXT("Array(%s)"), *Inner);
			return true;
		}
		if (const FSetProperty* Set = CastField<FSetProperty>(&Property))
		{
			FString Element;
			if (!BuildPropertyTypeDescriptor(*Set->ElementProp, Element, Context, FieldPath + TEXT("{}")))
			{
				return false;
			}
			OutType = FString::Printf(TEXT("Set(%s)"), *Element);
			return true;
		}
		if (const FMapProperty* Map = CastField<FMapProperty>(&Property))
		{
			FString Key;
			FString Value;
			if (!BuildPropertyTypeDescriptor(*Map->KeyProp, Key, Context, FieldPath + TEXT("{}.Key"))
				|| !BuildPropertyTypeDescriptor(*Map->ValueProp, Value, Context, FieldPath + TEXT("{}.Value")))
			{
				return false;
			}
			OutType = FString::Printf(TEXT("Map(%s,%s)"), *Key, *Value);
			return true;
		}
		if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(&Property))
		{
			FString Value;
			if (!BuildPropertyTypeDescriptor(
				*Optional->GetValueProperty(), Value, Context, FieldPath + TEXT("?")))
			{
				return false;
			}
			OutType = FString::Printf(TEXT("Optional(%s)"), *Value);
			return true;
		}
		if (Property.IsA<FBoolProperty>())
		{
			OutType = TEXT("Bool");
			return true;
		}
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(&Property))
		{
			FString Underlying;
			if (!NumericTypeName(*Enum->GetUnderlyingProperty(), Underlying))
			{
				return Context.Fail(
					ESeinDeterministicValueDigestResult::UnsupportedProperty,
					FieldPath,
					TEXT("Enum has an unsupported underlying integer type."));
			}
			OutType = FString::Printf(TEXT("Enum(%s,%s)"),
				Enum->GetEnum() ? *Enum->GetEnum()->GetPathName() : TEXT("<none>"),
				*Underlying);
			return true;
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(&Property))
		{
			if (Numeric->IsFloatingPoint() || !Numeric->IsInteger()
				|| !NumericTypeName(*Numeric, OutType))
			{
				return Context.Fail(
					ESeinDeterministicValueDigestResult::UnsupportedProperty,
					FieldPath,
					TEXT("Floating-point or unknown numeric fields are not canonical state."));
			}
			return true;
		}
		if (Property.IsA<FNameProperty>())
		{
			OutType = TEXT("Name");
			return true;
		}
		if (Property.IsA<FStrProperty>())
		{
			OutType = TEXT("String");
			return true;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				OutType = TEXT("InstancedStruct");
				return true;
			}
			if (!IsMarkedDeterministic(StructProperty->Struct, Context.Options))
			{
				return Context.Fail(
					ESeinDeterministicValueDigestResult::NonDeterministicStruct,
					FieldPath,
					TEXT("Nested struct is not marked SeinDeterministic."));
			}
			OutType = FString::Printf(TEXT("Struct(%s)"),
				*StructProperty->Struct->GetPathName());
			return true;
		}

		return Context.Fail(
			ESeinDeterministicValueDigestResult::UnsupportedProperty,
			FieldPath,
			FString::Printf(TEXT("Unsupported reflected property kind '%s'."),
				*Property.GetClass()->GetName()));
	}

	bool EncodePropertyValue(
		const FProperty& Property,
		const void* ValuePtr,
		FEncodeContext& Context,
		TArray<uint8>& Out,
		int32 Depth,
		const FString& FieldPath);

	bool EncodeStructValue(
		const UScriptStruct* Struct,
		const void* StructMemory,
		FEncodeContext& Context,
		TArray<uint8>& Out,
		int32 Depth,
		const FString& FieldPath);

	bool EncodeInstancedStructValue(
		const FInstancedStruct& Value,
		FEncodeContext& Context,
		TArray<uint8>& Out,
		int32 Depth,
		const FString& FieldPath,
		bool bCountElement)
	{
		if (!Context.CheckDepth(Depth))
		{
			return false;
		}
		if (!Value.IsValid() || !Value.GetScriptStruct() || !Value.GetMemory())
		{
			return Context.Fail(
				ESeinDeterministicValueDigestResult::InvalidInstancedStruct,
				FieldPath,
				TEXT("FInstancedStruct has no valid concrete value."));
		}
		if (bCountElement && !Context.AddElements(1))
		{
			return false;
		}

		TArray<uint8> EncodedValue;
		if (!EncodeStructValue(
			Value.GetScriptStruct(), Value.GetMemory(), Context, EncodedValue,
			Depth + 1, FieldPath + TEXT("<value>")))
		{
			switch (Context.Error.Result)
			{
			case ESeinDeterministicValueDigestResult::NonDeterministicStruct:
			case ESeinDeterministicValueDigestResult::UnsupportedProperty:
			case ESeinDeterministicValueDigestResult::InvalidRoot:
			case ESeinDeterministicValueDigestResult::InvalidInstancedStruct:
				return Context.Fail(
					ESeinDeterministicValueDigestResult::InvalidInstancedStruct,
					FieldPath,
					TEXT("FInstancedStruct concrete value is not canonical-digest compatible."),
					true);
			default:
				return false;
			}
		}

		return AppendByte(Context, Out, static_cast<uint8>(EValueToken::InstancedStruct))
			&& AppendUtf8(Context, Out, Value.GetScriptStruct()->GetPathName())
			&& AppendFramedBytes(Context, Out, EncodedValue);
	}

	bool EncodeNumericValue(
		const FNumericProperty& Numeric,
		const void* ValuePtr,
		FEncodeContext& Context,
		TArray<uint8>& Out)
	{
		const int32 Width = Numeric.GetElementSize();
		const bool bUnsigned = Numeric.IsA<FByteProperty>()
			|| Numeric.IsA<FUInt16Property>()
			|| Numeric.IsA<FUInt32Property>()
			|| Numeric.IsA<FUInt64Property>();
		const uint64 Bits = bUnsigned
			? Numeric.GetUnsignedIntPropertyValue(ValuePtr)
			: static_cast<uint64>(Numeric.GetSignedIntPropertyValue(ValuePtr));
		return AppendByte(Context, Out, static_cast<uint8>(EValueToken::Integer))
			&& AppendByte(Context, Out, static_cast<uint8>(Width))
			&& AppendUIntBigEndian(Context, Out, Bits, Width);
	}

	bool EncodePropertyValue(
		const FProperty& Property,
		const void* ValuePtr,
		FEncodeContext& Context,
		TArray<uint8>& Out,
		int32 Depth,
		const FString& FieldPath)
	{
		if (!ValuePtr)
		{
			return Context.Fail(
				ESeinDeterministicValueDigestResult::InvalidRoot,
				FieldPath,
				TEXT("Reflected property has no readable value memory."));
		}
		if (!Context.CheckDepth(Depth))
		{
			return false;
		}

		if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
		{
			FScriptArrayHelper Helper(Array, ValuePtr);
			if (!Context.AddElements(static_cast<uint64>(Helper.Num()))
				|| !AppendByte(Context, Out, static_cast<uint8>(EValueToken::Array))
				|| !AppendUInt64(Context, Out, static_cast<uint64>(Helper.Num())))
			{
				return false;
			}
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				TArray<uint8> Element;
				if (!EncodePropertyValue(
					*Array->Inner, Helper.GetRawPtr(Index), Context, Element,
					Depth + 1, FString::Printf(TEXT("%s[%d]"), *FieldPath, Index))
					|| !AppendFramedBytes(Context, Out, Element))
				{
					return false;
				}
			}
			return true;
		}
		if (const FSetProperty* Set = CastField<FSetProperty>(&Property))
		{
			FScriptSetHelper Helper(Set, ValuePtr);
			if (!Context.AddElements(static_cast<uint64>(Helper.Num())))
			{
				return false;
			}
			TArray<TArray<uint8>> Entries;
			Entries.Reserve(Helper.Num());
			uint64 EncodedEntryBytes = 0;
			uint64 NestedElements = 0;
			bool bByteLimitExceeded = false;
			bool bElementLimitExceeded = false;
			TOptional<FSeinDeterministicValueDigestError> BestError;
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}
				FEncodeContext EntryContext(Context.Options);
				TArray<uint8> Entry;
				if (!EncodePropertyValue(
					*Set->ElementProp, Helper.GetElementPtr(Index), EntryContext, Entry,
					Depth + 1, FieldPath + TEXT("{}")))
				{
					ConsiderError(BestError, EntryContext.Error);
					continue;
				}
				if (NestedElements > Context.Options.MaxAggregateElements
					|| EntryContext.AggregateElements
						> Context.Options.MaxAggregateElements - NestedElements)
				{
					bElementLimitExceeded = true;
				}
				else
				{
					NestedElements += EntryContext.AggregateElements;
				}

				const uint64 FramedSize = static_cast<uint64>(Entry.Num()) + sizeof(uint64);
				if (bByteLimitExceeded
					|| FramedSize > Context.Options.MaxEncodedBytes
					|| EncodedEntryBytes > Context.Options.MaxEncodedBytes - FramedSize)
				{
					bByteLimitExceeded = true;
					Entries.Empty();
					continue;
				}
				EncodedEntryBytes += FramedSize;
				Entries.Add(MoveTemp(Entry));
			}
			if (Context.AggregateElements > Context.Options.MaxAggregateElements
				|| NestedElements > Context.Options.MaxAggregateElements
					- Context.AggregateElements)
			{
				bElementLimitExceeded = true;
			}
			if (bByteLimitExceeded)
			{
				ConsiderError(BestError,
					ESeinDeterministicValueDigestResult::ByteLimitExceeded,
					TEXT("$bytes"), TEXT("Canonical value byte limit exceeded."));
			}
			if (bElementLimitExceeded)
			{
				ConsiderError(BestError,
					ESeinDeterministicValueDigestResult::ElementLimitExceeded,
					TEXT("$elements"),
					TEXT("Canonical value aggregate element limit exceeded."));
			}
			if (!ApplyError(Context, BestError)
				|| !Context.AddElements(NestedElements))
			{
				return false;
			}
			Entries.Sort([](const TArray<uint8>& A, const TArray<uint8>& B)
			{
				return BytesLess(A, B);
			});
			if (!AppendByte(Context, Out, static_cast<uint8>(EValueToken::Set))
				|| !AppendUInt64(Context, Out, static_cast<uint64>(Entries.Num())))
			{
				return false;
			}
			for (const TArray<uint8>& Entry : Entries)
			{
				if (!AppendFramedBytes(Context, Out, Entry))
				{
					return false;
				}
			}
			return true;
		}
		if (const FMapProperty* Map = CastField<FMapProperty>(&Property))
		{
			FScriptMapHelper Helper(Map, ValuePtr);
			if (!Context.AddElements(static_cast<uint64>(Helper.Num())))
			{
				return false;
			}
			TArray<TArray<uint8>> Entries;
			Entries.Reserve(Helper.Num());
			uint64 EncodedEntryBytes = 0;
			uint64 NestedElements = 0;
			bool bByteLimitExceeded = false;
			bool bElementLimitExceeded = false;
			TOptional<FSeinDeterministicValueDigestError> BestError;
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}
				TArray<uint8> Key;
				TArray<uint8> Value;
				FEncodeContext EntryContext(Context.Options);
				if (!EncodePropertyValue(
					*Map->KeyProp, Helper.GetKeyPtr(Index), EntryContext, Key,
					Depth + 1, FieldPath + TEXT("{}.Key"))
					|| !EncodePropertyValue(
						*Map->ValueProp, Helper.GetValuePtr(Index), EntryContext, Value,
						Depth + 1, FieldPath + TEXT("{}.Value")))
				{
					ConsiderError(BestError, EntryContext.Error);
					continue;
				}
				if (NestedElements > Context.Options.MaxAggregateElements
					|| EntryContext.AggregateElements
						> Context.Options.MaxAggregateElements - NestedElements)
				{
					bElementLimitExceeded = true;
				}
				else
				{
					NestedElements += EntryContext.AggregateElements;
				}

				FEncodeContext FramingContext(Context.Options);
				TArray<uint8> Entry;
				if (!AppendFramedBytes(FramingContext, Entry, Key)
					|| !AppendFramedBytes(FramingContext, Entry, Value))
				{
					ConsiderError(BestError, FramingContext.Error);
					continue;
				}
				const uint64 FramedSize = static_cast<uint64>(Entry.Num()) + sizeof(uint64);
				if (bByteLimitExceeded
					|| FramedSize > Context.Options.MaxEncodedBytes
					|| EncodedEntryBytes > Context.Options.MaxEncodedBytes - FramedSize)
				{
					bByteLimitExceeded = true;
					Entries.Empty();
					continue;
				}
				EncodedEntryBytes += FramedSize;
				Entries.Add(MoveTemp(Entry));
			}
			if (Context.AggregateElements > Context.Options.MaxAggregateElements
				|| NestedElements > Context.Options.MaxAggregateElements
					- Context.AggregateElements)
			{
				bElementLimitExceeded = true;
			}
			if (bByteLimitExceeded)
			{
				ConsiderError(BestError,
					ESeinDeterministicValueDigestResult::ByteLimitExceeded,
					TEXT("$bytes"), TEXT("Canonical value byte limit exceeded."));
			}
			if (bElementLimitExceeded)
			{
				ConsiderError(BestError,
					ESeinDeterministicValueDigestResult::ElementLimitExceeded,
					TEXT("$elements"),
					TEXT("Canonical value aggregate element limit exceeded."));
			}
			if (!ApplyError(Context, BestError)
				|| !Context.AddElements(NestedElements))
			{
				return false;
			}
			Entries.Sort([](const TArray<uint8>& A, const TArray<uint8>& B)
			{
				return BytesLess(A, B);
			});
			if (!AppendByte(Context, Out, static_cast<uint8>(EValueToken::Map))
				|| !AppendUInt64(Context, Out, static_cast<uint64>(Entries.Num())))
			{
				return false;
			}
			for (const TArray<uint8>& Entry : Entries)
			{
				if (!AppendFramedBytes(Context, Out, Entry))
				{
					return false;
				}
			}
			return true;
		}
		if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(&Property))
		{
			const void* OptionalValue = Optional->GetValuePointerForReadIfSet(ValuePtr);
			if (!AppendByte(Context, Out, static_cast<uint8>(EValueToken::Optional))
				|| !AppendByte(Context, Out, OptionalValue ? 1 : 0))
			{
				return false;
			}
			if (!OptionalValue)
			{
				return true;
			}
			return Context.AddElements(1)
				&& EncodePropertyValue(
					*Optional->GetValueProperty(), OptionalValue, Context, Out,
					Depth + 1, FieldPath + TEXT("?"));
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(&Property))
		{
			return AppendByte(Context, Out, static_cast<uint8>(EValueToken::Bool))
				&& AppendByte(Context, Out, Bool->GetPropertyValue(ValuePtr) ? 1 : 0);
		}
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(&Property))
		{
			return AppendByte(Context, Out, static_cast<uint8>(EValueToken::Enum))
				&& EncodeNumericValue(*Enum->GetUnderlyingProperty(), ValuePtr, Context, Out);
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(&Property))
		{
			if (Numeric->IsFloatingPoint() || !Numeric->IsInteger())
			{
				return Context.Fail(
					ESeinDeterministicValueDigestResult::UnsupportedProperty,
					FieldPath,
					TEXT("Floating-point fields are not canonical state."));
			}
			return EncodeNumericValue(*Numeric, ValuePtr, Context, Out);
		}
		if (Property.IsA<FNameProperty>())
		{
			return AppendByte(Context, Out, static_cast<uint8>(EValueToken::Name))
				&& AppendUtf8(Context, Out,
					CanonicalNameString(*static_cast<const FName*>(ValuePtr)));
		}
		if (Property.IsA<FStrProperty>())
		{
			return AppendByte(Context, Out, static_cast<uint8>(EValueToken::String))
				&& AppendUtf8(Context, Out, *static_cast<const FString*>(ValuePtr));
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				return EncodeInstancedStructValue(
					*static_cast<const FInstancedStruct*>(ValuePtr), Context, Out,
					Depth, FieldPath, true);
			}
			return EncodeStructValue(
				StructProperty->Struct, ValuePtr, Context, Out, Depth, FieldPath);
		}

		return Context.Fail(
			ESeinDeterministicValueDigestResult::UnsupportedProperty,
			FieldPath,
			TEXT("Unsupported reflected property value."));
	}

	bool EncodeStructValue(
		const UScriptStruct* Struct,
		const void* StructMemory,
		FEncodeContext& Context,
		TArray<uint8>& Out,
		int32 Depth,
		const FString& FieldPath)
	{
		if (!Struct || !StructMemory)
		{
			return Context.Fail(
				ESeinDeterministicValueDigestResult::InvalidRoot,
				FieldPath,
				TEXT("Struct type or value memory is null."));
		}
		if (!Context.CheckDepth(Depth))
		{
			return false;
		}
		if (!IsMarkedDeterministic(Struct, Context.Options))
		{
			return Context.Fail(
				ESeinDeterministicValueDigestResult::NonDeterministicStruct,
				FieldPath,
				TEXT("Struct is not marked SeinDeterministic."));
		}
		if (!AppendByte(Context, Out, static_cast<uint8>(EValueToken::Struct))
			|| !AppendUtf8(Context, Out, Struct->GetPathName()))
		{
			return false;
		}

		if (Struct == FGameplayTag::StaticStruct())
		{
			const FGameplayTag& Tag = *static_cast<const FGameplayTag*>(StructMemory);
			return AppendByte(Context, Out, static_cast<uint8>(EValueToken::GameplayTag))
				&& AppendUtf8(Context, Out, CanonicalNameString(Tag.GetTagName()));
		}
		if (Struct == FGameplayTagContainer::StaticStruct())
		{
			const TArray<FGameplayTag>& Tags =
				static_cast<const FGameplayTagContainer*>(StructMemory)->GetGameplayTagArray();
			if (!Context.AddElements(static_cast<uint64>(Tags.Num())))
			{
				return false;
			}
			TArray<FString> CanonicalTags;
			CanonicalTags.Reserve(Tags.Num());
			for (const FGameplayTag& Tag : Tags)
			{
				CanonicalTags.Add(CanonicalNameString(Tag.GetTagName()));
			}
			CanonicalTags.Sort([](const FString& A, const FString& B)
			{
				return A.Compare(B, ESearchCase::CaseSensitive) < 0;
			});
			if (!AppendByte(Context, Out,
					static_cast<uint8>(EValueToken::GameplayTagContainer))
				|| !AppendUInt64(Context, Out, static_cast<uint64>(CanonicalTags.Num())))
			{
				return false;
			}
			for (const FString& Tag : CanonicalTags)
			{
				if (!AppendUtf8(Context, Out, Tag))
				{
					return false;
				}
			}
			return true;
		}

		TArray<const FProperty*> Properties;
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			Properties.Add(*It);
		}
		Properties.Sort([](const FProperty& A, const FProperty& B)
		{
			const int32 OwnerOrder = PropertyOwnerPath(A).Compare(
				PropertyOwnerPath(B), ESearchCase::CaseSensitive);
			if (OwnerOrder != 0)
			{
				return OwnerOrder < 0;
			}
			return A.GetName().Compare(B.GetName(), ESearchCase::CaseSensitive) < 0;
		});

		if (!AppendUInt64(Context, Out, static_cast<uint64>(Properties.Num())))
		{
			return false;
		}
		for (const FProperty* Property : Properties)
		{
			const FString PropertyPath = PropertyFieldPath(FieldPath, *Property);
			FString TypeDescriptor;
			if (!BuildPropertyTypeDescriptor(
				*Property, TypeDescriptor, Context, PropertyPath)
				|| !AppendByte(Context, Out, static_cast<uint8>(EValueToken::Property))
				|| !AppendUtf8(Context, Out, PropertyOwnerPath(*Property))
				|| !AppendUtf8(Context, Out, Property->GetName())
				|| !AppendUtf8(Context, Out, TypeDescriptor)
				|| !AppendUInt64(Context, Out, static_cast<uint64>(Property->ArrayDim)))
			{
				return false;
			}
			if (Property->ArrayDim > 1
				&& !Context.AddElements(static_cast<uint64>(Property->ArrayDim)))
			{
				return false;
			}
			for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
			{
				const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(
					StructMemory, ArrayIndex);
				TArray<uint8> EncodedValue;
				const FString ValuePath = Property->ArrayDim > 1
					? FString::Printf(TEXT("%s[%d]"), *PropertyPath, ArrayIndex)
					: PropertyPath;
				if (!EncodePropertyValue(
					*Property, ValuePtr, Context, EncodedValue, Depth + 1, ValuePath)
					|| !AppendFramedBytes(Context, Out, EncodedValue))
				{
					return false;
				}
			}
		}
		return true;
	}

	uint32 ReadUInt32BigEndian(const uint8* Bytes)
	{
		return static_cast<uint32>(Bytes[0]) << 24
			| static_cast<uint32>(Bytes[1]) << 16
			| static_cast<uint32>(Bytes[2]) << 8
			| static_cast<uint32>(Bytes[3]);
	}

	bool BeginRootStream(FEncodeContext& Context, TArray<uint8>& Out)
	{
		return AppendUtf8(Context, Out, TEXT("SeinDeterministicValue"))
			&& AppendUInt16(Context, Out, CanonicalFormatVersion);
	}

	ESeinDeterministicValueDigestResult Finish(
		FEncodeContext& Context,
		const TArray<uint8>& CanonicalBytes,
		FGuid& OutDigest,
		FSeinDeterministicValueDigestError* OutError)
	{
		if (Context.Error.Result != ESeinDeterministicValueDigestResult::Success)
		{
			if (OutError)
			{
				*OutError = Context.Error;
			}
			return Context.Error.Result;
		}

		const FBlake3Hash Hash = FBlake3::HashBuffer(
			CanonicalBytes.GetData(), CanonicalBytes.Num());
		const uint8* Bytes = Hash.GetBytes();
		OutDigest = FGuid(
			ReadUInt32BigEndian(Bytes),
			ReadUInt32BigEndian(Bytes + 4),
			ReadUInt32BigEndian(Bytes + 8),
			ReadUInt32BigEndian(Bytes + 12));
		if (OutError)
		{
			OutError->Reset();
		}
		return ESeinDeterministicValueDigestResult::Success;
	}

	bool ValidateOptions(
		const FSeinDeterministicValueDigestOptions& Options,
		FEncodeContext& Context)
	{
		return (Options.MaxRecursionDepth >= 0 && Options.MaxEncodedBytes <= MaxTArrayBytes)
			|| Context.Fail(
				ESeinDeterministicValueDigestResult::InvalidOptions,
				TEXT("$options"),
				TEXT("Digest options require non-negative recursion and a byte cap representable by TArray."));
	}
}

ESeinDeterministicValueDigestResult FSeinDeterministicValueDigest::Compute(
	const UScriptStruct* Struct,
	const void* StructMemory,
	FGuid& OutDigest,
	FSeinDeterministicValueDigestError* OutError,
	const FSeinDeterministicValueDigestOptions& Options)
{
	OutDigest.Invalidate();
	if (OutError)
	{
		OutError->Reset();
	}

	FEncodeContext Context(Options);
	TArray<uint8> CanonicalBytes;
	if (ValidateOptions(Options, Context)
		&& BeginRootStream(Context, CanonicalBytes))
	{
		TArray<uint8> EncodedRoot;
		if (EncodeStructValue(
			Struct, StructMemory, Context, EncodedRoot, 0, TEXT("$")))
		{
			AppendFramedBytes(Context, CanonicalBytes, EncodedRoot);
		}
	}
	return Finish(Context, CanonicalBytes, OutDigest, OutError);
}

ESeinDeterministicValueDigestResult FSeinDeterministicValueDigest::Compute(
	const FInstancedStruct& Value,
	FGuid& OutDigest,
	FSeinDeterministicValueDigestError* OutError,
	const FSeinDeterministicValueDigestOptions& Options)
{
	OutDigest.Invalidate();
	if (OutError)
	{
		OutError->Reset();
	}

	FEncodeContext Context(Options);
	TArray<uint8> CanonicalBytes;
	if (ValidateOptions(Options, Context)
		&& BeginRootStream(Context, CanonicalBytes))
	{
		TArray<uint8> EncodedRoot;
		if (EncodeInstancedStructValue(
			Value, Context, EncodedRoot, 0, TEXT("$"), false))
		{
			AppendFramedBytes(Context, CanonicalBytes, EncodedRoot);
		}
	}
	return Finish(Context, CanonicalBytes, OutDigest, OutError);
}
