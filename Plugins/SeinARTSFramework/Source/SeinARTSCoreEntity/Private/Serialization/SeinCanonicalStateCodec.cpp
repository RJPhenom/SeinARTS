/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateCodec.cpp
 */

#include "Serialization/SeinCanonicalStateCodec.h"

#include "Containers/ContainerAllocationPolicies.h"
#include "Hash/Blake3.h"
#include "Serialization/SeinCanonicalWirePrimitives.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/PropertyOptional.h"
#include "UObject/PropertyTypeName.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

using namespace UE::Sein::CanonicalWirePrivate;

namespace
{
	class FWireSchemaDigestBuilder
	{
	public:
		bool Build(const UScriptStruct* Struct, FGuid& OutDigest, FString& OutError)
		{
			return Build(Struct, nullptr, OutDigest, OutError);
		}

		bool Build(
			const UStruct* Struct,
			FSeinWirePropertyFilter InPropertyFilter,
			FGuid& OutDigest,
			FString& OutError)
		{
			OutDigest.Invalidate();
			Error.Reset();
			PropertyFilter = InPropertyFilter;
			constexpr uint8 Domain[] = {
				'S', 'E', 'I', 'N', 'W', 'I', 'R', 'E', 'S', 'C', 'H', 2};
			Hasher.Update(Domain, UE_ARRAY_COUNT(Domain));
			if (!AppendStruct(Struct, 0))
			{
				OutError = MoveTemp(Error);
				return false;
			}

			const FBlake3Hash Hash = Hasher.Finalize();
			const uint8* Bytes = Hash.GetBytes();
			OutDigest = FGuid(
				ReadWord(Bytes), ReadWord(Bytes + 4),
				ReadWord(Bytes + 8), ReadWord(Bytes + 12));
			if (!OutDigest.IsValid()) OutDigest.D = 1;
			OutError.Reset();
			return true;
		}

	private:
		enum class EToken : uint8
		{
			Struct = 1,
			RecursiveStruct,
			GameplayTag,
			GameplayTagContainer,
			Property,
			Enum,
			End,
		};

		static uint32 ReadWord(const uint8* Bytes)
		{
			return (static_cast<uint32>(Bytes[0]) << 24)
				| (static_cast<uint32>(Bytes[1]) << 16)
				| (static_cast<uint32>(Bytes[2]) << 8)
				| static_cast<uint32>(Bytes[3]);
		}

		bool Fail(const FString& Message)
		{
			if (Error.IsEmpty()) Error = Message;
			return false;
		}

		void AppendToken(EToken Token)
		{
			const uint8 Value = static_cast<uint8>(Token);
			Hasher.Update(&Value, sizeof(Value));
		}

		void AppendUInt32(uint32 Value)
		{
			const uint8 Bytes[4] = {
				static_cast<uint8>(Value >> 24),
				static_cast<uint8>(Value >> 16),
				static_cast<uint8>(Value >> 8),
				static_cast<uint8>(Value)};
			Hasher.Update(Bytes, UE_ARRAY_COUNT(Bytes));
		}

		void AppendUInt64(uint64 Value)
		{
			AppendUInt32(static_cast<uint32>(Value >> 32));
			AppendUInt32(static_cast<uint32>(Value));
		}

		void AppendString(FStringView Value)
		{
			FTCHARToUTF8 Utf8(Value.GetData(), Value.Len());
			AppendUInt32(static_cast<uint32>(Utf8.Length()));
			Hasher.Update(Utf8.Get(), Utf8.Length());
		}

		void AppendEnum(const UEnum* Enum)
		{
			AppendToken(EToken::Enum);
			if (!Enum)
			{
				AppendString(TEXT("<none>"));
				AppendUInt32(0);
				AppendUInt32(0);
				return;
			}
			AppendString(Enum->GetPathName());
			AppendUInt32(
				Enum->HasAnyEnumFlags(EEnumFlags::Flags) ? 1u : 0u);
			AppendUInt32(static_cast<uint32>(Enum->NumEnums()));
			for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
			{
				AppendString(Enum->GetNameStringByIndex(Index));
				AppendUInt64(static_cast<uint64>(Enum->GetValueByIndex(Index)));
			}
		}

		bool AppendProperty(const FProperty& Property, int32 Depth)
		{
			if (Depth > 64)
			{
				return Fail(TEXT("wire schema property recursion limit exceeded"));
			}
			if (Property.HasAnyPropertyFlags(
				CPF_Transient | CPF_EditorOnly | CPF_Deprecated | CPF_SkipSerialization))
			{
				return Fail(FString::Printf(
					TEXT("wire schema contains a non-serialized property '%s'"),
					*Property.GetPathName()));
			}

			TStringBuilder<512> TypeName;
			TypeName << UE::FPropertyTypeName(&Property);
			AppendString(TypeName.ToView());

			if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
			{
				return AppendProperty(*Array->Inner, Depth + 1);
			}
			if (Property.IsA<FSetProperty>() || Property.IsA<FMapProperty>())
			{
				return Fail(FString::Printf(
					TEXT("wire schema contains unsupported unordered property '%s'"),
					*Property.GetPathName()));
			}
			if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(&Property))
			{
				return AppendProperty(*Optional->GetValueProperty(), Depth + 1);
			}
			if (const FEnumProperty* Enum = CastField<FEnumProperty>(&Property))
			{
				AppendEnum(Enum->GetEnum());
				const FNumericProperty* Underlying = Enum->GetUnderlyingProperty();
				return (Underlying && IntegerWireWidth(*Underlying) > 0)
					|| Fail(TEXT("wire schema contains an unsupported enum width"));
			}
			if (const FNumericProperty* Numeric = CastField<FNumericProperty>(&Property))
			{
				if (!Numeric->IsInteger() || Numeric->IsFloatingPoint()
					|| IntegerWireWidth(*Numeric) <= 0)
				{
					return Fail(FString::Printf(
						TEXT("wire schema contains unsupported numeric property '%s'"),
						*Property.GetPathName()));
				}
				if (const UEnum* Enum = Numeric->GetIntPropertyEnum()) AppendEnum(Enum);
				return true;
			}
			if (Property.IsA<FBoolProperty>()
				|| Property.IsA<FNameProperty>()
				|| Property.IsA<FStrProperty>())
			{
				return true;
			}
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
			{
				return StructProperty->Struct == FInstancedStruct::StaticStruct()
					|| AppendStruct(StructProperty->Struct, Depth + 1);
			}
			return Fail(FString::Printf(
				TEXT("wire schema contains unsupported property '%s' (%s)"),
				*Property.GetPathName(), *Property.GetClass()->GetName()));
		}

		bool AppendStruct(const UStruct* Struct, int32 Depth)
		{
			if (!Struct || Depth > 64)
			{
				return Fail(TEXT("wire schema contains an invalid or excessively recursive struct"));
			}
			if (Struct == FGameplayTag::StaticStruct())
			{
				AppendToken(EToken::GameplayTag);
				AppendString(Struct->GetPathName());
				return true;
			}
			if (Struct == FGameplayTagContainer::StaticStruct())
			{
				AppendToken(EToken::GameplayTagContainer);
				AppendString(Struct->GetPathName());
				return true;
			}

			const FString StructPath = Struct->GetPathName();
			if (Visiting.Contains(Struct))
			{
				AppendToken(EToken::RecursiveStruct);
				AppendString(StructPath);
				return true;
			}

			AppendToken(EToken::Struct);
			AppendString(StructPath);
			Visiting.Add(Struct);
			TArray<const FProperty*, TInlineAllocator<32>> Properties;
			for (TFieldIterator<FProperty> It(
				Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
			{
				if (!It->HasAnyPropertyFlags(CPF_EditorOnly)
					&& (!PropertyFilter || PropertyFilter(**It)))
				{
					Properties.Add(*It);
				}
			}
			AppendUInt32(static_cast<uint32>(Properties.Num()));
			for (const FProperty* Property : Properties)
			{
				AppendToken(EToken::Property);
				const UStruct* Owner = Property->GetOwnerStruct();
				AppendString(Owner ? Owner->GetPathName() : TEXT("<none>"));
				AppendString(Property->GetName());
				AppendUInt32(static_cast<uint32>(Property->ArrayDim));
				if (!AppendProperty(*Property, Depth + 1))
				{
					Visiting.Remove(Struct);
					return false;
				}
			}
			Visiting.Remove(Struct);
			AppendToken(EToken::End);
			return true;
		}

		FBlake3 Hasher;
		TSet<const UStruct*> Visiting;
		FString Error;
		FSeinWirePropertyFilter PropertyFilter = nullptr;
	};

	struct FValueContext
	{
		FValueContext(
			FSeinStructWireCatalogView InCatalog,
			const FSeinStructWireLimits& InLimits,
			FString& InError,
			FSeinWirePropertyFilter InPropertyFilter = nullptr,
			bool bInUseBoundedInlineNames = false)
			: Catalog(InCatalog)
			, Limits(InLimits)
			, Error(InError)
			, PropertyFilter(InPropertyFilter)
			, bUseBoundedInlineNames(bInUseBoundedInlineNames)
		{
		}

		bool Fail(const FString& Message)
		{
			if (Error.IsEmpty()) Error = Message;
			return false;
		}

		bool CheckDepth(int32 Depth)
		{
			return Depth <= Limits.MaxRecursionDepth
				|| Fail(TEXT("wire reflected-value recursion limit exceeded"));
		}

		bool AddElements(uint64 Count)
		{
			const uint64 Max = Limits.MaxAggregateElements < 0
				? 0 : static_cast<uint64>(Limits.MaxAggregateElements);
			if (Count > Max || Elements > Max - Count)
			{
				return Fail(TEXT("wire reflected-value aggregate element limit exceeded"));
			}
			Elements += Count;
			return true;
		}

		bool AddNativeAllocation(uint64 Bytes)
		{
			const int32 NativeLimit = EffectiveNativeAllocationLimit(Limits);
			const uint64 Max = NativeLimit < 0
				? 0 : static_cast<uint64>(NativeLimit);
			if (Bytes > Max || NativeAllocationBytes > Max - Bytes)
			{
				return Fail(TEXT("wire native-allocation byte limit exceeded"));
			}
			NativeAllocationBytes += Bytes;
			return true;
		}

		bool AddNativeString(const FString& Text)
		{
			return AddNativeAllocation(NativeDecodedStringBytes(Text));
		}

		bool ShouldSerialize(const FProperty& Property) const
		{
			return !Property.HasAnyPropertyFlags(CPF_EditorOnly)
				&& (!PropertyFilter || PropertyFilter(Property));
		}

		int32 DynamicIndex(const UScriptStruct* Struct) const
		{
			for (int32 Index = 0; Index < Catalog.DynamicStructs.Num(); ++Index)
			{
				if (Catalog.DynamicStructs[Index] == Struct) return Index;
			}
			return INDEX_NONE;
		}

		int32 NameIndex(FName Name) const
		{
			for (int32 Index = 0; Index < Catalog.Names.Num(); ++Index)
			{
				if (Catalog.Names[Index] == Name) return Index;
			}
			return INDEX_NONE;
		}

		FSeinStructWireCatalogView Catalog;
		const FSeinStructWireLimits& Limits;
		FString& Error;
		FSeinWirePropertyFilter PropertyFilter = nullptr;
		bool bUseBoundedInlineNames = false;
		uint64 Elements = 0;
		uint64 NativeAllocationBytes = 0;
	};

	uint64 CountGameplayTagParents(const FString& TagText)
	{
		uint64 Count = 0;
		for (const TCHAR Character : TagText)
		{
			Count += Character == TEXT('.');
		}
		return Count;
	}

	int64 SignedEnumValueFromWire(
		const FNumericProperty& Storage,
		uint64 Encoded)
	{
		alignas(uint64) uint8 ValueBytes[sizeof(uint64)] = {};
		Storage.SetIntPropertyValue(ValueBytes, Encoded);
		return Storage.GetSignedIntPropertyValue(ValueBytes);
	}

	bool ValidateEnumValue(
		const UEnum* Enum,
		int64 Value,
		FValueContext& Context)
	{
		if (!Enum)
		{
			return Context.Fail(
				TEXT("wire enum has no declared domain"));
		}
		// UEnum's ordinary validator includes the generated _MAX sentinel,
		// although FEnumProperty treats that value as invalid user state.
		// Flag UEnums instead admit OR-combinations of declared bits; their
		// helper already omits the generated sentinel. Integer Bitmask
		// properties have no attached UEnum here and remain numeric storage.
		const bool bFlags =
			Enum->HasAnyEnumFlags(EEnumFlags::Flags);
		const bool bValid = bFlags
			? Enum->IsValidEnumValueOrBitfield(Value)
			: Enum->IsValidEnumValue(Value)
				&& Value != Enum->GetMaxEnumValue();
		if (!bValid)
		{
			return Context.Fail(FString::Printf(
				TEXT("wire enum value is outside declared domain '%s'"),
				*Enum->GetPathName()));
		}
		return true;
	}

	bool CalculateGrowingArrayUpperBoundBytes(
		uint64 Count,
		int32 ElementSize,
		uint32 ElementAlignment,
		uint64& OutBytes)
	{
		OutBytes = 0;
		if (Count == 0)
		{
			return true;
		}
		if (Count > static_cast<uint64>(MAX_int32)
			|| ElementSize <= 0)
		{
			return false;
		}
		const int32 NewNum = static_cast<int32>(Count);
		const int32 PreviousCapacity = NewNum > 1
			? NewNum - 1
			: 0;
		const int32 Capacity =
			DefaultCalculateSlackGrow<int32>(
				NewNum,
				PreviousCapacity,
				static_cast<SIZE_T>(ElementSize),
				true,
				ElementAlignment);
		if (Capacity < NewNum
			|| static_cast<uint64>(Capacity)
				> MAX_uint64 / static_cast<uint64>(ElementSize))
		{
			return false;
		}
		OutBytes = static_cast<uint64>(Capacity)
			* static_cast<uint64>(ElementSize);
		return true;
	}

	bool EncodeStruct(
		const UStruct* Struct, const void* Memory, FWireWriter& Writer,
		FValueContext& Context, int32 Depth);
	bool DecodeStruct(
		const UStruct* Struct, void* Memory, FWireReader& Reader,
		FValueContext& Context, int32 Depth);

	bool EncodeProperty(
		const FProperty& Property, const void* Value, FWireWriter& Writer,
		FValueContext& Context, int32 Depth)
	{
		if (!Value || !Context.CheckDepth(Depth)) return false;
		if (Property.HasAnyPropertyFlags(
			CPF_Transient | CPF_EditorOnly | CPF_Deprecated | CPF_SkipSerialization))
		{
			return Context.Fail(TEXT("wire struct contains a non-serialized property"));
		}

		if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
		{
			FScriptArrayHelper Helper(Array, Value);
			uint64 ArrayBytes = 0;
			if (!Context.AddElements(static_cast<uint64>(Helper.Num()))
				|| !CalculateGrowingArrayUpperBoundBytes(
					static_cast<uint64>(Helper.Num()),
					Array->Inner->GetElementSize(),
					Array->Inner->GetMinAlignment(),
					ArrayBytes)
				|| !Context.AddNativeAllocation(ArrayBytes)
				|| !Writer.U32(static_cast<uint32>(Helper.Num()))) return false;
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				// Per-element framing gives the decoder a remaining-byte lower bound
				// before it resizes the destination array, even for empty structs.
				TArray<uint8> EncodedElement;
				FWireWriter ElementWriter(
					EncodedElement, Context.Limits.MaxBytes, Context.Error);
				if (!EncodeProperty(
					*Array->Inner, Helper.GetRawPtr(Index), ElementWriter,
					Context, Depth + 1)
					|| !Writer.U32(static_cast<uint32>(EncodedElement.Num()))
					|| !Writer.Raw(EncodedElement.GetData(), EncodedElement.Num())) return false;
			}
			return true;
		}
		if (Property.IsA<FSetProperty>() || Property.IsA<FMapProperty>())
		{
			return Context.Fail(
				TEXT("unordered containers are not supported on the canonical wire"));
		}
		if (const FOptionalProperty* Optional = CastField<FOptionalProperty>(&Property))
		{
			const void* OptionalValue = Optional->GetValuePointerForReadIfSet(Value);
			if (!Writer.U8(OptionalValue ? 1 : 0)) return false;
			return !OptionalValue
				|| (Context.AddElements(1)
					&& EncodeProperty(*Optional->GetValueProperty(), OptionalValue, Writer, Context, Depth + 1));
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(&Property))
		{
			return Writer.U8(Bool->GetPropertyValue(Value) ? 1 : 0);
		}
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(&Property))
		{
			const FNumericProperty* Underlying =
				Enum->GetUnderlyingProperty();
			const int32 Width =
				Underlying ? IntegerWireWidth(*Underlying) : 0;
			if (Width <= 0)
			{
				return Context.Fail(
					TEXT("unsupported deterministic enum width"));
			}
			return ValidateEnumValue(
					Enum->GetEnum(),
					Underlying->GetSignedIntPropertyValue(Value),
					Context)
				&& Writer.UInt(
					Underlying->GetUnsignedIntPropertyValue(Value),
					Width);
		}
		if (const FNumericProperty* Numeric =
			CastField<FNumericProperty>(&Property))
		{
			if (!Numeric->IsInteger() || Numeric->IsFloatingPoint())
			{
				return Context.Fail(
					TEXT("floating-point values are forbidden on the deterministic wire"));
			}
			const int32 Width = IntegerWireWidth(*Numeric);
			if (Width <= 0)
			{
				return Context.Fail(
					TEXT("unsupported deterministic integer width"));
			}
			const UEnum* Enum = Numeric->GetIntPropertyEnum();
			return (!Enum
					|| ValidateEnumValue(
						Enum,
						Numeric->GetSignedIntPropertyValue(Value),
						Context))
				&& Writer.UInt(
					Numeric->GetUnsignedIntPropertyValue(Value),
					Width);
		}
		if (Property.IsA<FNameProperty>())
		{
			const FName Name = *static_cast<const FName*>(Value);
			if (Context.bUseBoundedInlineNames)
			{
				if (Name.IsNone())
				{
					return Writer.U8(0);
				}
				const FString Text = Name.ToString();
				return Context.AddNativeString(Text)
					&& Writer.U8(1)
					&& Writer.Utf8(
						Text,
						Context.Limits.MaxStringBytes);
			}
			if (Name.IsNone())
			{
				return Writer.U32(0);
			}
			const int32 Index = Context.NameIndex(Name);
			return Index != INDEX_NONE
				? Writer.U32(static_cast<uint32>(Index) + 1u)
				: Context.Fail(
					TEXT("wire name is outside the frozen catalog"));
		}
		if (Property.IsA<FStrProperty>())
		{
			const FString& Text = *static_cast<const FString*>(Value);
			return Context.AddNativeString(Text)
				&& Writer.Utf8(
					Text, Context.Limits.MaxStringBytes);
		}
		if (const FStructProperty* StructProperty =
			CastField<FStructProperty>(&Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				const FInstancedStruct& Dynamic =
					*static_cast<const FInstancedStruct*>(Value);
				const int32 Index = Dynamic.IsValid()
					? Context.DynamicIndex(Dynamic.GetScriptStruct())
					: INDEX_NONE;
				if (Index == INDEX_NONE)
				{
					return Context.Fail(
						TEXT("wire dynamic struct is absent or outside the frozen catalog"));
				}
				return Context.AddElements(1)
					&& Context.AddNativeAllocation(
						static_cast<uint64>(
							Dynamic.GetScriptStruct()->GetStructureSize()))
					&& Writer.U32(static_cast<uint32>(Index))
					&& EncodeStruct(
						Dynamic.GetScriptStruct(),
						Dynamic.GetMemory(),
						Writer,
						Context,
						Depth + 1);
			}
			return EncodeStruct(
				StructProperty->Struct,
				Value,
				Writer,
				Context,
				Depth + 1);
		}
		return Context.Fail(FString::Printf(
			TEXT("unsupported reflected wire property '%s'"),
			*Property.GetClass()->GetName()));
	}

	bool DecodeProperty(
		const FProperty& Property, void* Value, FWireReader& Reader,
		FValueContext& Context, int32 Depth)
	{
		if (!Value || !Context.CheckDepth(Depth))
		{
			return false;
		}
		if (Property.HasAnyPropertyFlags(
			CPF_Transient | CPF_EditorOnly | CPF_Deprecated
				| CPF_SkipSerialization))
		{
			return Context.Fail(
				TEXT("wire struct contains a non-serialized property"));
		}

		if (const FArrayProperty* Array =
			CastField<FArrayProperty>(&Property))
		{
			uint32 Count = 0;
			if (!Reader.U32(Count) || !Context.AddElements(Count))
			{
				return false;
			}
			if (Count > static_cast<uint32>(MAX_int32)
				|| static_cast<uint64>(Count) * ArrayElementFrameBytes
					> static_cast<uint64>(Reader.Remaining()))
			{
				return Context.Fail(
					TEXT("wire array count exceeds its aggregate or remaining-byte bound"));
			}
			uint64 ArrayBytes = 0;
			if (!CalculateGrowingArrayUpperBoundBytes(
					Count,
					Array->Inner->GetElementSize(),
					Array->Inner->GetMinAlignment(),
					ArrayBytes)
				|| !Context.AddNativeAllocation(ArrayBytes))
			{
				return Context.Fail(
					TEXT("wire array expands beyond its decoded-allocation budget"));
			}
			FScriptArrayHelper Helper(Array, Value);
			// Resize preserves locally authored defaults for policy-excluded
			// fields in existing elements. The pessimistic grow-capacity charge
			// above covers Resize's allocator slack before it can allocate.
			Helper.Resize(static_cast<int32>(Count));
			for (uint32 Index = 0; Index < Count; ++Index)
			{
				uint32 ElementFrameBytes = 0;
				TConstArrayView<uint8> ElementView;
				if (!Reader.U32(ElementFrameBytes)
					|| ElementFrameBytes > static_cast<uint32>(MAX_int32)
					|| !Reader.Slice(
						static_cast<int32>(ElementFrameBytes),
						ElementView))
				{
					return false;
				}
				FWireReader ElementReader(ElementView, Context.Error);
				if (!DecodeProperty(
						*Array->Inner,
						Helper.GetRawPtr(static_cast<int32>(Index)),
						ElementReader,
						Context,
						Depth + 1)
					|| !ElementReader.AtEnd())
				{
					if (Context.Error.IsEmpty())
					{
						Context.Error =
							TEXT("trailing bytes in wire array element");
					}
					return false;
				}
			}
			return true;
		}
		if (Property.IsA<FSetProperty>() || Property.IsA<FMapProperty>())
		{
			return Context.Fail(
				TEXT("unordered containers are not supported on the canonical wire"));
		}
		if (const FOptionalProperty* Optional =
			CastField<FOptionalProperty>(&Property))
		{
			uint8 bSet = 0;
			if (!Reader.U8(bSet) || bSet > 1)
			{
				return Context.Fail(TEXT("invalid wire optional marker"));
			}
			if (!bSet)
			{
				Optional->MarkUnset(Value);
				return true;
			}
			if (!Context.AddElements(1))
			{
				return false;
			}
			void* OptionalValue =
				Optional->MarkSetAndGetInitializedValuePointerToReplace(
					Value);
			return DecodeProperty(
				*Optional->GetValueProperty(),
				OptionalValue,
				Reader,
				Context,
				Depth + 1);
		}
		if (const FBoolProperty* Bool =
			CastField<FBoolProperty>(&Property))
		{
			uint8 Encoded = 0;
			if (!Reader.U8(Encoded) || Encoded > 1)
			{
				return Context.Fail(TEXT("invalid wire boolean"));
			}
			Bool->SetPropertyValue(Value, Encoded != 0);
			return true;
		}
		if (const FEnumProperty* Enum =
			CastField<FEnumProperty>(&Property))
		{
			FNumericProperty* Underlying =
				Enum->GetUnderlyingProperty();
			const int32 Width =
				Underlying ? IntegerWireWidth(*Underlying) : 0;
			uint64 Encoded = 0;
			if (Width <= 0 || !Reader.UInt(Encoded, Width))
			{
				return Context.Fail(
					TEXT("unsupported deterministic enum width"));
			}
			if (!ValidateEnumValue(
					Enum->GetEnum(),
					SignedEnumValueFromWire(*Underlying, Encoded),
					Context))
			{
				return false;
			}
			Underlying->SetIntPropertyValue(Value, Encoded);
			return true;
		}
		if (const FNumericProperty* Numeric =
			CastField<FNumericProperty>(&Property))
		{
			if (!Numeric->IsInteger() || Numeric->IsFloatingPoint())
			{
				return Context.Fail(
					TEXT("floating-point values are forbidden on the deterministic wire"));
			}
			const int32 Width = IntegerWireWidth(*Numeric);
			uint64 Encoded = 0;
			if (Width <= 0 || !Reader.UInt(Encoded, Width))
			{
				return Context.Fail(
					TEXT("unsupported deterministic integer width"));
			}
			if (const UEnum* Enum = Numeric->GetIntPropertyEnum();
				Enum
				&& !ValidateEnumValue(
					Enum,
					SignedEnumValueFromWire(*Numeric, Encoded),
					Context))
			{
				return false;
			}
			Numeric->SetIntPropertyValue(Value, Encoded);
			return true;
		}
		if (Property.IsA<FNameProperty>())
		{
			if (Context.bUseBoundedInlineNames)
			{
				uint8 bHasName = 0;
				if (!Reader.U8(bHasName) || bHasName > 1)
				{
					return Context.Fail(
						TEXT("invalid wire name marker"));
				}
				if (!bHasName)
				{
					*static_cast<FName*>(Value) = NAME_None;
					return true;
				}
				FString Text;
				auto ChargeNative = [&Context](uint64 Bytes)
				{
					return Context.AddNativeAllocation(Bytes);
				};
				FName Name;
				if (!Reader.Utf8(
						Text,
						Context.Limits.MaxStringBytes,
						ChargeNative)
					|| !ResolveExistingName(
						Text, Name, Context.Error))
				{
					return false;
				}
				*static_cast<FName*>(Value) = Name;
				return true;
			}
			uint32 Encoded = 0;
			if (!Reader.U32(Encoded))
			{
				return false;
			}
			if (Encoded == 0)
			{
				*static_cast<FName*>(Value) = NAME_None;
				return true;
			}
			const uint32 Index = Encoded - 1u;
			if (Index
				>= static_cast<uint32>(Context.Catalog.Names.Num()))
			{
				return Context.Fail(
					TEXT("wire name index is outside the frozen catalog"));
			}
			*static_cast<FName*>(Value) =
				Context.Catalog.Names[static_cast<int32>(Index)];
			return true;
		}
		if (Property.IsA<FStrProperty>())
		{
			auto ChargeNative = [&Context](uint64 Bytes)
			{
				return Context.AddNativeAllocation(Bytes);
			};
			return Reader.Utf8(
				*static_cast<FString*>(Value),
				Context.Limits.MaxStringBytes,
				ChargeNative);
		}
		if (const FStructProperty* StructProperty =
			CastField<FStructProperty>(&Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				uint32 Index = 0;
				if (!Reader.U32(Index)
					|| Index
						>= static_cast<uint32>(
							Context.Catalog.DynamicStructs.Num()))
				{
					return Context.Fail(
						TEXT("wire dynamic struct index is outside the frozen catalog"));
				}
				const UScriptStruct* ExactType =
					Context.Catalog.DynamicStructs[
						static_cast<int32>(Index)];
				if (!ExactType
					|| !Context.AddElements(1)
					|| !Context.AddNativeAllocation(
						static_cast<uint64>(
							ExactType->GetStructureSize())))
				{
					return Context.Fail(
						TEXT("wire dynamic struct catalog entry is invalid"));
				}
				FInstancedStruct& Dynamic =
					*static_cast<FInstancedStruct*>(Value);
				Dynamic.InitializeAs(ExactType);
				return DecodeStruct(
					ExactType,
					Dynamic.GetMutableMemory(),
					Reader,
					Context,
					Depth + 1);
			}
			return DecodeStruct(
				StructProperty->Struct,
				Value,
				Reader,
				Context,
				Depth + 1);
		}
		return Context.Fail(FString::Printf(
			TEXT("unsupported reflected wire property '%s'"),
			*Property.GetClass()->GetName()));
	}

	bool EncodeStruct(
		const UStruct* Struct, const void* Memory,
		FWireWriter& Writer, FValueContext& Context, int32 Depth)
	{
		if (!Struct || !Memory || !Context.CheckDepth(Depth))
		{
			return Context.Fail(TEXT("invalid reflected wire struct"));
		}
		if (Struct == FSeinEntityHandle::StaticStruct())
		{
			return WriteEntity(
				Writer,
				*static_cast<const FSeinEntityHandle*>(Memory));
		}
		if (Struct == FGameplayTag::StaticStruct())
		{
			const FGameplayTag Tag =
				*static_cast<const FGameplayTag*>(Memory);
			if (Tag.IsValid()
				&& !Context.AddNativeString(Tag.ToString()))
			{
				return false;
			}
			return WriteTag(
				Writer,
				Tag,
				Context.Limits.MaxStringBytes);
		}
		if (Struct == FGameplayTagContainer::StaticStruct())
		{
			const TArray<FGameplayTag>& Tags =
				static_cast<const FGameplayTagContainer*>(Memory)
					->GetGameplayTagArray();
			uint64 ExplicitTagBytes = 0;
			uint64 ParentCount = 0;
			for (const FGameplayTag& Tag : Tags)
			{
				ParentCount += CountGameplayTagParents(
					Tag.ToString());
			}
			uint64 ParentTagBytes = 0;
			if (!Context.AddElements(
					static_cast<uint64>(Tags.Num()))
				|| !Context.AddElements(ParentCount)
				|| !CalculateGrowingArrayUpperBoundBytes(
					static_cast<uint64>(Tags.Num()),
					sizeof(FGameplayTag),
					alignof(FGameplayTag),
					ExplicitTagBytes)
				|| !CalculateGrowingArrayUpperBoundBytes(
					ParentCount,
					sizeof(FGameplayTag),
					alignof(FGameplayTag),
					ParentTagBytes)
				|| !Context.AddNativeAllocation(
					ExplicitTagBytes)
				|| !Context.AddNativeAllocation(
					ParentTagBytes)
				|| !Writer.U32(static_cast<uint32>(Tags.Num())))
			{
				return false;
			}
			for (const FGameplayTag& Tag : Tags)
			{
				const FString TagText = Tag.ToString();
				if (!Context.AddNativeString(TagText)
					|| !Writer.Utf8(
						TagText,
						Context.Limits.MaxStringBytes))
				{
					return false;
				}
			}
			return true;
		}

		for (TFieldIterator<FProperty> It(
				Struct, EFieldIterationFlags::IncludeSuper);
			It;
			++It)
		{
			const FProperty& Property = **It;
			if (!Context.ShouldSerialize(Property))
			{
				continue;
			}
			for (int32 Index = 0;
				Index < Property.ArrayDim;
				++Index)
			{
				if (!EncodeProperty(
					Property,
					Property.ContainerPtrToValuePtr<void>(
						Memory, Index),
					Writer,
					Context,
					Depth + 1))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool DecodeStruct(
		const UStruct* Struct, void* Memory,
		FWireReader& Reader, FValueContext& Context, int32 Depth)
	{
		if (!Struct || !Memory || !Context.CheckDepth(Depth))
		{
			return Context.Fail(TEXT("invalid reflected wire struct"));
		}
		if (Struct == FSeinEntityHandle::StaticStruct())
		{
			return ReadEntity(
				Reader,
				*static_cast<FSeinEntityHandle*>(Memory));
		}
		if (Struct == FGameplayTag::StaticStruct())
		{
			auto ChargeNative = [&Context](uint64 Bytes)
			{
				return Context.AddNativeAllocation(Bytes);
			};
			return ReadTag(
				Reader,
				*static_cast<FGameplayTag*>(Memory),
				Context.Limits.MaxStringBytes,
				ChargeNative,
				Context.Error);
		}
		if (Struct == FGameplayTagContainer::StaticStruct())
		{
			uint32 Count = 0;
			uint64 ExplicitTagBytes = 0;
			if (!Reader.U32(Count)
				|| !Context.AddElements(Count)
				|| !CalculateGrowingArrayUpperBoundBytes(
					Count,
					sizeof(FGameplayTag),
					alignof(FGameplayTag),
					ExplicitTagBytes)
				|| !Context.AddNativeAllocation(
					ExplicitTagBytes))
			{
				return false;
			}
			FGameplayTagContainer& Container =
				*static_cast<FGameplayTagContainer*>(Memory);
			Container.Reset();
			uint64 MaximumParentCount = 0;
			uint64 ChargedParentBytes = 0;
			for (uint32 Index = 0; Index < Count; ++Index)
			{
				FString Text;
				FGameplayTag Tag;
				auto ChargeNative = [&Context](uint64 Bytes)
				{
					return Context.AddNativeAllocation(Bytes);
				};
				if (!Reader.Utf8(Text, Context.Limits.MaxStringBytes, ChargeNative)
					|| !ResolveExistingTag(Text, Tag, Context.Error))
				{
					return false;
				}
				const uint64 ParentCount =
					CountGameplayTagParents(Text);
				if (ParentCount
						> MAX_uint64 - MaximumParentCount)
				{
					return Context.Fail(
						TEXT("wire tag parent count overflows"));
				}
				MaximumParentCount += ParentCount;
				uint64 ParentTagBytes = 0;
				if (!Context.AddElements(ParentCount)
					|| !CalculateGrowingArrayUpperBoundBytes(
						MaximumParentCount,
						sizeof(FGameplayTag),
						alignof(FGameplayTag),
						ParentTagBytes)
					|| ParentTagBytes < ChargedParentBytes
					|| !Context.AddNativeAllocation(
						ParentTagBytes
							- ChargedParentBytes))
				{
					return false;
				}
				ChargedParentBytes = ParentTagBytes;
				if (Container.HasTagExact(Tag))
				{
					return Context.Fail(
						TEXT("duplicate tag in wire tag container"));
				}
				Container.AddTag(Tag);
			}
			return true;
		}

		for (TFieldIterator<FProperty> It(
				Struct, EFieldIterationFlags::IncludeSuper);
			It;
			++It)
		{
			const FProperty& Property = **It;
			if (!Context.ShouldSerialize(Property))
			{
				continue;
			}
			for (int32 Index = 0;
				Index < Property.ArrayDim;
				++Index)
			{
				if (!DecodeProperty(
					Property,
					Property.ContainerPtrToValuePtr<void>(
						Memory, Index),
					Reader,
					Context,
					Depth + 1))
				{
					return false;
				}
			}
		}
		return true;
	}

}

bool FSeinCanonicalStateCodec::ComputeSchemaDigest(
	const UScriptStruct* Struct,
	FGuid& OutDigest,
	FString& OutError)
{
	FWireSchemaDigestBuilder Builder;
	return Builder.Build(Struct, OutDigest, OutError);
}

bool FSeinCanonicalStateCodec::EncodeWithCost(
	const UScriptStruct* Struct,
	const void* StructMemory,
	FSeinStructWireCatalogView Catalog,
	const FSeinStructWireLimits& Limits,
	TArray<uint8>& OutBytes,
	FString& OutError,
	FSeinWireCost& OutCost)
{
	OutCost = {};
	if (!Struct || !StructMemory || Limits.MaxBytes < 0
		|| Limits.MaxAggregateElements < 0 || Limits.MaxStringBytes < 0
		|| Limits.MaxRecursionDepth < 0
		|| EffectiveNativeAllocationLimit(Limits) < 0)
	{
		OutBytes.Reset();
		OutError = TEXT("invalid bounded-struct wire arguments");
		return false;
	}
	FWireWriter Writer(OutBytes, Limits.MaxBytes, OutError, true);
	FValueContext Context(Catalog, Limits, OutError);
	if (!Context.AddNativeAllocation(static_cast<uint64>(Struct->GetStructureSize()))
		|| !EncodeStruct(Struct, StructMemory, Writer, Context, 0))
	{
		OutBytes.Reset();
		return false;
	}
	if (!BuildCanonicalCost(
		OutBytes.Num(), Context.Elements, OutCost.CanonicalCostBytes, OutError))
	{
		OutBytes.Reset();
		return false;
	}
	OutCost.NativeAllocationBytes = Context.NativeAllocationBytes;
	return true;
}

bool FSeinCanonicalStateCodec::Encode(
	const UScriptStruct* Struct,
	const void* StructMemory,
	FSeinStructWireCatalogView Catalog,
	const FSeinStructWireLimits& Limits,
	TArray<uint8>& OutBytes,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	FSeinWireCost Cost;
	if (!EncodeWithCost(
		Struct, StructMemory, Catalog, Limits,
		OutBytes, OutError, Cost)) return false;
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = Cost.NativeAllocationBytes;
	return true;
}

bool FSeinCanonicalStateCodec::DecodeWithCost(
	TConstArrayView<uint8> Bytes,
	const UScriptStruct* Struct,
	void* OutStructMemory,
	FSeinStructWireCatalogView Catalog,
	const FSeinStructWireLimits& Limits,
	FString& OutError,
	FSeinWireCost& OutCost)
{
	OutCost = {};
	if (!Struct || !OutStructMemory || Limits.MaxBytes < 0
		|| Bytes.Num() > Limits.MaxBytes || Limits.MaxAggregateElements < 0
		|| Limits.MaxStringBytes < 0 || Limits.MaxRecursionDepth < 0
		|| EffectiveNativeAllocationLimit(Limits) < 0)
	{
		OutError = TEXT("invalid or oversized bounded-struct wire input");
		return false;
	}

	FWireReader Reader(Bytes, OutError, true);
	FValueContext Context(Catalog, Limits, OutError);
	if (!Context.AddNativeAllocation(static_cast<uint64>(Struct->GetStructureSize())))
	{
		return false;
	}
	FStructOnScope Scratch(Struct);
	if (!DecodeStruct(Struct, Scratch.GetStructMemory(), Reader, Context, 0)
		|| !Reader.AtEnd())
	{
		if (OutError.IsEmpty()) OutError = TEXT("trailing bytes after bounded reflected value");
		return false;
	}
	if (!BuildCanonicalCost(
		Bytes.Num(), Context.Elements, OutCost.CanonicalCostBytes, OutError))
	{
		return false;
	}
	OutCost.NativeAllocationBytes = Context.NativeAllocationBytes;
	Struct->CopyScriptStruct(OutStructMemory, Scratch.GetStructMemory());
	return true;
}

bool FSeinCanonicalStateCodec::Decode(
	TConstArrayView<uint8> Bytes,
	const UScriptStruct* Struct,
	void* OutStructMemory,
	FSeinStructWireCatalogView Catalog,
	const FSeinStructWireLimits& Limits,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes) *OutDecodedAllocationBytes = 0;
	FSeinWireCost Cost;
	if (!DecodeWithCost(
		Bytes, Struct, OutStructMemory, Catalog,
		Limits, OutError, Cost)) return false;
	if (OutDecodedAllocationBytes)
		*OutDecodedAllocationBytes = Cost.NativeAllocationBytes;
	return true;
}

bool FSeinCanonicalStateCodec::ValidateObjectClass(
	const UClass* Class,
	FSeinWirePropertyFilter PropertyFilter,
	FString& OutError)
{
	FGuid UnusedDigest;
	return ComputeObjectSchemaDigest(
		Class, PropertyFilter, UnusedDigest, OutError);
}

bool FSeinCanonicalStateCodec::ComputeObjectSchemaDigest(
	const UClass* Class,
	FSeinWirePropertyFilter PropertyFilter,
	FGuid& OutDigest,
	FString& OutError)
{
	FWireSchemaDigestBuilder Builder;
	return Builder.Build(
		Class, PropertyFilter, OutDigest, OutError);
}

bool FSeinCanonicalStateCodec::EncodeObject(
	const UObject& Object,
	FSeinStructWireCatalogView Catalog,
	const FSeinStructWireLimits& Limits,
	FSeinWirePropertyFilter PropertyFilter,
	TArray<uint8>& OutBytes,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes)
	{
		*OutDecodedAllocationBytes = 0;
	}
	const UClass* Class = Object.GetClass();
	if (!Class || Limits.MaxBytes < 0
		|| Limits.MaxAggregateElements < 0
		|| Limits.MaxStringBytes < 0
		|| Limits.MaxRecursionDepth < 0
		|| EffectiveNativeAllocationLimit(Limits) < 0)
	{
		OutBytes.Reset();
		OutError = TEXT("invalid bounded-object wire arguments");
		return false;
	}

	FWireWriter Writer(OutBytes, Limits.MaxBytes, OutError, true);
	FValueContext Context(
		Catalog, Limits, OutError, PropertyFilter,
		/*bUseBoundedInlineNames=*/true);
	if (!Context.AddNativeAllocation(
			static_cast<uint64>(Class->GetPropertiesSize()))
		|| !EncodeStruct(
			Class, &Object, Writer, Context, 0))
	{
		OutBytes.Reset();
		return false;
	}
	if (OutDecodedAllocationBytes)
	{
		*OutDecodedAllocationBytes =
			Context.NativeAllocationBytes;
	}
	return true;
}

bool FSeinCanonicalStateCodec::DecodeObject(
	TConstArrayView<uint8> Bytes,
	UObject& OutObject,
	FSeinStructWireCatalogView Catalog,
	const FSeinStructWireLimits& Limits,
	FSeinWirePropertyFilter PropertyFilter,
	FString& OutError,
	uint64* OutDecodedAllocationBytes)
{
	if (OutDecodedAllocationBytes)
	{
		*OutDecodedAllocationBytes = 0;
	}
	const UClass* Class = OutObject.GetClass();
	if (!Class || Limits.MaxBytes < 0
		|| Bytes.Num() > Limits.MaxBytes
		|| Limits.MaxAggregateElements < 0
		|| Limits.MaxStringBytes < 0
		|| Limits.MaxRecursionDepth < 0
		|| EffectiveNativeAllocationLimit(Limits) < 0)
	{
		OutError =
			TEXT("invalid or oversized bounded-object wire input");
		return false;
	}

	FWireReader Reader(Bytes, OutError, true);
	FValueContext Context(
		Catalog, Limits, OutError, PropertyFilter,
		/*bUseBoundedInlineNames=*/true);
	if (!Context.AddNativeAllocation(
			static_cast<uint64>(Class->GetPropertiesSize()))
		|| !DecodeStruct(
			Class, &OutObject, Reader, Context, 0)
		|| !Reader.AtEnd())
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("trailing bytes after bounded object state");
		}
		return false;
	}
	if (OutDecodedAllocationBytes)
	{
		*OutDecodedAllocationBytes =
			Context.NativeAllocationBytes;
	}
	return true;
}
