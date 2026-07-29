#include "Serialization/SeinCanonicalReflectedStateDigest.h"

#include "GameplayTagContainer.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalStatePropertyPolicy.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Class.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr uint32 ReflectedSchemaFormatVersion = 2;
	constexpr uint32 ReflectedValueFormatVersion = 2;

	FString CanonicalNameText(const FName Name)
	{
		FString Result = Name.ToString();
		for (TCHAR& Character : Result)
		{
			if (Character >= TEXT('A') && Character <= TEXT('Z'))
			{
				Character = Character - TEXT('A') + TEXT('a');
			}
		}
		return Result;
	}

	FString PropertyOwnerPath(const FProperty& Property)
	{
		const UStruct* Owner = Property.GetOwnerStruct();
		return Owner ? Owner->GetPathName() : FString();
	}

	FString ChildPath(
		const FString& Parent,
		const FProperty& Property)
	{
		return Parent.IsEmpty()
			? Property.GetName()
			: Parent + TEXT(".") + Property.GetName();
	}

	bool GuidLess(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A) return A.A < B.A;
		if (A.B != B.B) return A.B < B.B;
		if (A.C != B.C) return A.C < B.C;
		return A.D < B.D;
	}

	bool IsUnsignedNumeric(const FNumericProperty& Numeric)
	{
		return Numeric.IsA<FByteProperty>()
			|| Numeric.IsA<FUInt16Property>()
			|| Numeric.IsA<FUInt32Property>()
			|| Numeric.IsA<FUInt64Property>();
	}

	FString NumericTypeName(const FNumericProperty& Numeric)
	{
		if (Numeric.IsA<FByteProperty>()) return TEXT("UInt8");
		if (Numeric.IsA<FInt8Property>()) return TEXT("Int8");
		if (Numeric.IsA<FInt16Property>()) return TEXT("Int16");
		if (Numeric.IsA<FUInt16Property>()) return TEXT("UInt16");
		if (Numeric.IsA<FIntProperty>()) return TEXT("Int32");
		if (Numeric.IsA<FUInt32Property>()) return TEXT("UInt32");
		if (Numeric.IsA<FInt64Property>()) return TEXT("Int64");
		if (Numeric.IsA<FUInt64Property>()) return TEXT("UInt64");
		return FString();
	}

	bool ShouldSkipProperty(const FProperty& Property)
	{
		return FSeinCanonicalStatePropertyPolicy::ShouldSkip(
			Property);
	}

	void GatherCanonicalProperties(
		const UStruct* Type,
		TArray<const FProperty*>& OutProperties)
	{
		OutProperties.Reset();
		if (!Type)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(
			Type, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			if (!ShouldSkipProperty(**It))
			{
				OutProperties.Add(*It);
			}
		}
		OutProperties.Sort([](const FProperty& A, const FProperty& B)
		{
			const int32 OwnerOrder = PropertyOwnerPath(A).Compare(
				PropertyOwnerPath(B), ESearchCase::CaseSensitive);
			if (OwnerOrder != 0)
			{
				return OwnerOrder < 0;
			}
			return A.GetName().Compare(
				B.GetName(), ESearchCase::CaseSensitive) < 0;
		});
	}

	struct FProjectionContext
	{
		explicit FProjectionContext(
			const FSeinCanonicalReflectedStateLimits& InLimits)
			: Limits(InLimits)
		{
		}

		const FSeinCanonicalReflectedStateLimits& Limits;
		FString Error;
		int64 AggregateElements = 0;
		int64 TotalStringCharacters = 0;
		int32 InstancedObjects = 0;
		int32 UnorderedContainerDepth = 0;
		TArray<const UObject*> ObjectStack;
		TMap<const UObject*, uint32> ObjectEncounterIDs;
		TMap<const UStruct*, FGuid> SchemaDigests;
		TArray<const UStruct*> SchemaStack;

		bool Fail(const FString& FieldPath, const FString& Message)
		{
			if (Error.IsEmpty())
			{
				Error = FieldPath.IsEmpty()
					? Message
					: FString::Printf(
						TEXT("%s: %s"), *FieldPath, *Message);
			}
			return false;
		}

		bool CheckDepth(const int32 Depth, const FString& FieldPath)
		{
			if (Depth < 0 || Depth > Limits.MaxRecursionDepth)
			{
				return Fail(
					FieldPath,
					TEXT("Reflected-state recursion limit exceeded."));
			}
			return true;
		}

		bool AddElements(const int64 Count, const FString& FieldPath)
		{
			if (Count < 0
				|| Count > Limits.MaxAggregateElements
				|| AggregateElements
					> static_cast<int64>(Limits.MaxAggregateElements) - Count)
			{
				return Fail(
					FieldPath,
					TEXT("Reflected-state aggregate element limit exceeded."));
			}
			AggregateElements += Count;
			return true;
		}

		bool Write(
			FSeinCanonicalDigestWriter& Writer,
			const bool bWrote,
			const FString& FieldPath)
		{
			return bWrote
				|| Fail(
					FieldPath,
					Writer.GetError().IsEmpty()
						? TEXT("Canonical reflected-state writer failed.")
						: Writer.GetError());
		}

		bool WriteString(
			FSeinCanonicalDigestWriter& Writer,
			const FString& Value,
			const FString& FieldPath)
		{
			const int64 Characters = Value.Len();
			if (Characters > Limits.MaxStringCharacters
				|| TotalStringCharacters
					> static_cast<int64>(
						Limits.MaxTotalStringCharacters) - Characters)
			{
				return Fail(
					FieldPath,
					TEXT("Reflected-state string limit exceeded."));
			}
			TotalStringCharacters += Characters;
			return Write(Writer, Writer.WriteString(Value), FieldPath);
		}

		bool WriteName(
			FSeinCanonicalDigestWriter& Writer,
			const FName Value,
			const FString& FieldPath)
		{
			const FString Text = Value.ToString();
			const int64 Characters = Text.Len();
			if (Characters > Limits.MaxStringCharacters
				|| TotalStringCharacters
					> static_cast<int64>(
						Limits.MaxTotalStringCharacters) - Characters)
			{
				return Fail(
					FieldPath,
					TEXT("Reflected-state name limit exceeded."));
			}
			TotalStringCharacters += Characters;
			return Write(Writer, Writer.WriteName(Value), FieldPath);
		}
	};

	bool WriteTypeSchema(
		const UStruct* Type,
		FProjectionContext& Context,
		FSeinCanonicalDigestWriter& Writer,
		int32 Depth,
		const FString& FieldPath);

	bool ResolveSchemaDigest(
		const UStruct* Type,
		FProjectionContext& Context,
		const FString& FieldPath,
		FGuid& OutDigest);

	bool WritePropertySchema(
		const FProperty& Property,
		FProjectionContext& Context,
		FSeinCanonicalDigestWriter& Writer,
		const int32 Depth,
		const FString& FieldPath)
	{
		if (!Context.CheckDepth(Depth, FieldPath))
		{
			return false;
		}
		if (ShouldSkipProperty(Property))
		{
			return Context.Fail(
				FieldPath,
				TEXT("A skipped presentation/transient field reached schema encoding."));
		}

		const auto Kind = [&Context, &Writer, &FieldPath](
			const TCHAR* Name)
		{
			return Context.WriteString(Writer, Name, FieldPath);
		};

		if (const FArrayProperty* Array =
			CastField<FArrayProperty>(&Property))
		{
			return Kind(TEXT("Array"))
				&& WritePropertySchema(
					*Array->Inner, Context, Writer, Depth + 1,
					FieldPath + TEXT("[]"));
		}
		if (const FSetProperty* Set = CastField<FSetProperty>(&Property))
		{
			return Kind(TEXT("Set"))
				&& WritePropertySchema(
					*Set->ElementProp, Context, Writer, Depth + 1,
					FieldPath + TEXT("{}"));
		}
		if (const FMapProperty* Map = CastField<FMapProperty>(&Property))
		{
			return Kind(TEXT("Map"))
				&& WritePropertySchema(
					*Map->KeyProp, Context, Writer, Depth + 1,
					FieldPath + TEXT("{}.Key"))
				&& WritePropertySchema(
					*Map->ValueProp, Context, Writer, Depth + 1,
					FieldPath + TEXT("{}.Value"));
		}
		if (const FOptionalProperty* Optional =
			CastField<FOptionalProperty>(&Property))
		{
			return Kind(TEXT("Optional"))
				&& WritePropertySchema(
					*Optional->GetValueProperty(),
					Context,
					Writer,
					Depth + 1,
					FieldPath + TEXT("?"));
		}
		if (Property.IsA<FBoolProperty>())
		{
			return Kind(TEXT("Bool"));
		}
		if (const FEnumProperty* Enum =
			CastField<FEnumProperty>(&Property))
		{
			const FNumericProperty* Underlying =
				Enum->GetUnderlyingProperty();
			const FString NumericName =
				Underlying ? NumericTypeName(*Underlying) : FString();
			if (!Underlying || NumericName.IsEmpty())
			{
				return Context.Fail(
					FieldPath,
					TEXT("Enum has an unsupported underlying integer type."));
			}
			return Kind(TEXT("Enum"))
				&& Context.WriteString(
					Writer,
					Enum->GetEnum()
						? Enum->GetEnum()->GetPathName()
						: FString(),
					FieldPath)
				&& Context.WriteString(
					Writer, NumericName, FieldPath);
		}
		if (const FNumericProperty* Numeric =
			CastField<FNumericProperty>(&Property))
		{
			const FString NumericName = NumericTypeName(*Numeric);
			if (Numeric->IsFloatingPoint()
				|| !Numeric->IsInteger()
				|| NumericName.IsEmpty())
			{
				return Context.Fail(
					FieldPath,
					TEXT("Floating-point or unknown numeric fields are not canonical state."));
			}
			if (!Kind(TEXT("Integer"))
				|| !Context.WriteString(
					Writer, NumericName, FieldPath))
			{
				return false;
			}
			const UEnum* ByteEnum = Numeric->GetIntPropertyEnum();
			return Context.Write(
					Writer, Writer.WriteBool(ByteEnum != nullptr), FieldPath)
				&& (!ByteEnum
					|| Context.WriteString(
						Writer, ByteEnum->GetPathName(), FieldPath));
		}
		if (Property.IsA<FNameProperty>())
		{
			return Kind(TEXT("Name"));
		}
		if (Property.IsA<FStrProperty>())
		{
			return Kind(TEXT("String"));
		}
		if (Property.IsA<FTextProperty>())
		{
			return Context.Fail(
				FieldPath,
				TEXT("FText is presentation state and is unsupported inside canonical containers."));
		}
		if (const FStructProperty* StructProperty =
			CastField<FStructProperty>(&Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (!Struct)
			{
				return Context.Fail(
					FieldPath,
					TEXT("Struct property has no reflected type."));
			}
			if (Struct == FInstancedStruct::StaticStruct())
			{
				return Kind(TEXT("InstancedStruct"));
			}
			if (Struct == FGameplayTag::StaticStruct())
			{
				return Kind(TEXT("GameplayTag"));
			}
			if (Struct == FGameplayTagContainer::StaticStruct())
			{
				return Kind(TEXT("GameplayTagContainer"));
			}
			if (Struct == FSoftObjectPath::StaticStruct())
			{
				return Kind(TEXT("SoftObjectPath"));
			}
			if (Struct == FSoftClassPath::StaticStruct())
			{
				return Kind(TEXT("SoftClassPath"));
			}
			if (Struct == FTopLevelAssetPath::StaticStruct())
			{
				return Kind(TEXT("TopLevelAssetPath"));
			}
			FGuid NestedSchemaDigest;
			return Kind(TEXT("Struct"))
				&& Context.WriteString(
					Writer, Struct->GetPathName(), FieldPath)
				&& ResolveSchemaDigest(
					Struct,
					Context,
					FieldPath + TEXT(".<schema>"),
					NestedSchemaDigest)
				&& Context.Write(
					Writer,
					Writer.WriteGuid(NestedSchemaDigest),
					FieldPath);
		}
		if (const FSoftClassProperty* SoftClass =
			CastField<FSoftClassProperty>(&Property))
		{
			return Kind(TEXT("SoftClass"))
				&& Context.WriteString(
					Writer,
					SoftClass->MetaClass
						? SoftClass->MetaClass->GetPathName()
						: FString(),
					FieldPath);
		}
		if (const FSoftObjectProperty* SoftObject =
			CastField<FSoftObjectProperty>(&Property))
		{
			return Kind(TEXT("SoftObject"))
				&& Context.WriteString(
					Writer,
					SoftObject->PropertyClass
						? SoftObject->PropertyClass->GetPathName()
						: FString(),
					FieldPath);
		}
		if (const FClassProperty* Class =
			CastField<FClassProperty>(&Property))
		{
			return Kind(TEXT("Class"))
				&& Context.WriteString(
					Writer,
					Class->MetaClass
						? Class->MetaClass->GetPathName()
						: FString(),
					FieldPath);
		}
		if (Property.IsA<FWeakObjectProperty>()
			|| Property.IsA<FLazyObjectProperty>())
		{
			return Context.Fail(
				FieldPath,
				TEXT("Weak and lazy runtime object references are not canonical state."));
		}
		if (const FObjectPropertyBase* Object =
			CastField<FObjectPropertyBase>(&Property))
		{
			const bool bInstanced = Property.HasAnyPropertyFlags(
				CPF_InstancedReference | CPF_ContainsInstancedReference);
			return Kind(bInstanced
					? TEXT("InstancedObject")
					: TEXT("Object"))
				&& Context.WriteString(
					Writer,
					Object->PropertyClass
						? Object->PropertyClass->GetPathName()
						: FString(),
					FieldPath);
		}

		return Context.Fail(
			FieldPath,
			FString::Printf(
				TEXT("Unsupported reflected property kind '%s'."),
				*Property.GetClass()->GetName()));
	}

	bool WriteTypeSchema(
		const UStruct* Type,
		FProjectionContext& Context,
		FSeinCanonicalDigestWriter& Writer,
		const int32 Depth,
		const FString& FieldPath)
	{
		if (!Type)
		{
			return Context.Fail(
				FieldPath, TEXT("Reflected schema type is null."));
		}
		if (!Context.CheckDepth(Depth, FieldPath)
			|| !Context.WriteString(
				Writer, Type->GetPathName(), FieldPath))
		{
			return false;
		}

		TArray<const FProperty*> Properties;
		GatherCanonicalProperties(Type, Properties);
		if (!Context.Write(
			Writer,
			Writer.WriteUInt32(static_cast<uint32>(Properties.Num())),
			FieldPath))
		{
			return false;
		}
		for (const FProperty* Property : Properties)
		{
			const FString PropertyPath =
				ChildPath(FieldPath, *Property);
			if (!Context.WriteString(
					Writer, PropertyOwnerPath(*Property), PropertyPath)
				|| !Context.WriteName(
					Writer, Property->GetFName(), PropertyPath)
				|| !Context.Write(
					Writer,
					Writer.WriteInt32(Property->ArrayDim),
					PropertyPath)
				|| !WritePropertySchema(
					*Property,
					Context,
					Writer,
					Depth + 1,
					PropertyPath))
			{
				return false;
			}
		}
		return true;
	}

	bool ResolveSchemaDigest(
		const UStruct* Type,
		FProjectionContext& Context,
		const FString& FieldPath,
		FGuid& OutDigest)
	{
		if (!Type)
		{
			return Context.Fail(
				FieldPath, TEXT("Dynamic reflected schema type is null."));
		}
		if (const FGuid* Existing = Context.SchemaDigests.Find(Type))
		{
			OutDigest = *Existing;
			return true;
		}
		if (Context.SchemaStack.Contains(Type))
		{
			return Context.Fail(
				FieldPath,
				TEXT("Recursive reflected schema cycle detected."));
		}

		Context.SchemaStack.Add(Type);
		FSeinCanonicalDigestWriter SchemaWriter(
			TEXT("SeinARTS.ReflectedState.Schema"),
			ReflectedSchemaFormatVersion);
		const bool bWrote = WriteTypeSchema(
			Type,
			Context,
			SchemaWriter,
			0,
			FieldPath);
		Context.SchemaStack.Pop();
		if (!bWrote)
		{
			return false;
		}
		FString SchemaError;
		if (!SchemaWriter.Finalize(OutDigest, SchemaError))
		{
			return Context.Fail(
				FieldPath,
				SchemaError.IsEmpty()
					? TEXT("Dynamic reflected schema digest failed.")
					: SchemaError);
		}
		Context.SchemaDigests.Add(Type, OutDigest);
		return true;
	}

	bool WriteReflectedValue(
		const UStruct* Type,
		const void* Memory,
		FProjectionContext& Context,
		FSeinCanonicalDigestWriter& Writer,
		int32 Depth,
		const FString& FieldPath);

	bool FinalizeChildDigest(
		FSeinCanonicalDigestWriter& ChildWriter,
		FProjectionContext& Context,
		const FString& FieldPath,
		FGuid& OutDigest)
	{
		FString ChildError;
		if (!ChildWriter.Finalize(OutDigest, ChildError))
		{
			return Context.Fail(
				FieldPath,
				ChildError.IsEmpty()
					? TEXT("Canonical unordered-entry digest failed.")
					: ChildError);
		}
		return true;
	}

	bool WritePropertyValue(
		const FProperty& Property,
		const void* ValuePtr,
		FProjectionContext& Context,
		FSeinCanonicalDigestWriter& Writer,
		const int32 Depth,
		const FString& FieldPath)
	{
		if (!ValuePtr)
		{
			return Context.Fail(
				FieldPath, TEXT("Reflected property value is null."));
		}
		if (!Context.CheckDepth(Depth, FieldPath))
		{
			return false;
		}
		if (ShouldSkipProperty(Property))
		{
			return Context.Fail(
				FieldPath,
				TEXT("A skipped presentation/transient field reached value encoding."));
		}

		if (const FArrayProperty* Array =
			CastField<FArrayProperty>(&Property))
		{
			FScriptArrayHelper Helper(Array, ValuePtr);
			if (!Context.AddElements(Helper.Num(), FieldPath)
				|| !Context.Write(
					Writer,
					Writer.WriteUInt32(
						static_cast<uint32>(Helper.Num())),
					FieldPath))
			{
				return false;
			}
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				if (!WritePropertyValue(
					*Array->Inner,
					Helper.GetRawPtr(Index),
					Context,
					Writer,
					Depth + 1,
					FString::Printf(
						TEXT("%s[%d]"), *FieldPath, Index)))
				{
					return false;
				}
			}
			return true;
		}
		if (const FSetProperty* Set = CastField<FSetProperty>(&Property))
		{
			FScriptSetHelper Helper(Set, ValuePtr);
			if (!Context.AddElements(Helper.Num(), FieldPath))
			{
				return false;
			}
			TArray<FGuid> ElementDigests;
			ElementDigests.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}
				FSeinCanonicalDigestWriter ElementWriter(
					TEXT("SeinARTS.ReflectedState.SetElement"),
					ReflectedValueFormatVersion);
				const FString ElementPath = FString::Printf(
					TEXT("%s{%d}"), *FieldPath, Index);
				++Context.UnorderedContainerDepth;
				const bool bEncodedElement = WritePropertyValue(
						*Set->ElementProp,
						Helper.GetElementPtr(Index),
						Context,
						ElementWriter,
						Depth + 1,
						ElementPath);
				--Context.UnorderedContainerDepth;
				if (!bEncodedElement)
				{
					return false;
				}
				FGuid& ElementDigest =
					ElementDigests.AddDefaulted_GetRef();
				if (!FinalizeChildDigest(
					ElementWriter,
					Context,
					ElementPath,
					ElementDigest))
				{
					return false;
				}
			}
			ElementDigests.Sort(GuidLess);
			if (!Context.Write(
				Writer,
				Writer.WriteUInt32(
					static_cast<uint32>(ElementDigests.Num())),
				FieldPath))
			{
				return false;
			}
			for (const FGuid& Digest : ElementDigests)
			{
				if (!Context.Write(
					Writer, Writer.WriteGuid(Digest), FieldPath))
				{
					return false;
				}
			}
			return true;
		}
		if (const FMapProperty* Map = CastField<FMapProperty>(&Property))
		{
			FScriptMapHelper Helper(Map, ValuePtr);
			if (!Context.AddElements(Helper.Num(), FieldPath))
			{
				return false;
			}
			TArray<FGuid> EntryDigests;
			EntryDigests.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
			{
				if (!Helper.IsValidIndex(Index))
				{
					continue;
				}
				FSeinCanonicalDigestWriter EntryWriter(
					TEXT("SeinARTS.ReflectedState.MapEntry"),
					ReflectedValueFormatVersion);
				const FString EntryPath = FString::Printf(
					TEXT("%s{%d}"), *FieldPath, Index);
				++Context.UnorderedContainerDepth;
				const bool bEncodedEntry =
					WritePropertyValue(
						*Map->KeyProp,
						Helper.GetKeyPtr(Index),
						Context,
						EntryWriter,
						Depth + 1,
						EntryPath + TEXT(".Key"))
					&& WritePropertyValue(
						*Map->ValueProp,
						Helper.GetValuePtr(Index),
						Context,
						EntryWriter,
						Depth + 1,
						EntryPath + TEXT(".Value"));
				--Context.UnorderedContainerDepth;
				if (!bEncodedEntry)
				{
					return false;
				}
				FGuid& EntryDigest =
					EntryDigests.AddDefaulted_GetRef();
				if (!FinalizeChildDigest(
					EntryWriter,
					Context,
					EntryPath,
					EntryDigest))
				{
					return false;
				}
			}
			EntryDigests.Sort(GuidLess);
			if (!Context.Write(
				Writer,
				Writer.WriteUInt32(
					static_cast<uint32>(EntryDigests.Num())),
				FieldPath))
			{
				return false;
			}
			for (const FGuid& Digest : EntryDigests)
			{
				if (!Context.Write(
					Writer, Writer.WriteGuid(Digest), FieldPath))
				{
					return false;
				}
			}
			return true;
		}
		if (const FOptionalProperty* Optional =
			CastField<FOptionalProperty>(&Property))
		{
			const void* OptionalValue =
				Optional->GetValuePointerForReadIfSet(ValuePtr);
			if (!Context.Write(
				Writer,
				Writer.WriteBool(OptionalValue != nullptr),
				FieldPath))
			{
				return false;
			}
			return !OptionalValue
				|| (Context.AddElements(1, FieldPath)
					&& WritePropertyValue(
						*Optional->GetValueProperty(),
						OptionalValue,
						Context,
						Writer,
						Depth + 1,
						FieldPath + TEXT("?")));
		}
		if (const FBoolProperty* Bool =
			CastField<FBoolProperty>(&Property))
		{
			return Context.Write(
				Writer,
				Writer.WriteBool(Bool->GetPropertyValue(ValuePtr)),
				FieldPath);
		}
		if (const FEnumProperty* Enum =
			CastField<FEnumProperty>(&Property))
		{
			const FNumericProperty* Underlying =
				Enum->GetUnderlyingProperty();
			if (!Underlying)
			{
				return Context.Fail(
					FieldPath,
					TEXT("Enum has no underlying integer property."));
			}
			const uint64 Bits = IsUnsignedNumeric(*Underlying)
				? Underlying->GetUnsignedIntPropertyValue(ValuePtr)
				: static_cast<uint64>(
					Underlying->GetSignedIntPropertyValue(ValuePtr));
			return Context.Write(
				Writer, Writer.WriteUInt64(Bits), FieldPath);
		}
		if (const FNumericProperty* Numeric =
			CastField<FNumericProperty>(&Property))
		{
			if (Numeric->IsFloatingPoint()
				|| !Numeric->IsInteger()
				|| NumericTypeName(*Numeric).IsEmpty())
			{
				return Context.Fail(
					FieldPath,
					TEXT("Floating-point or unknown numeric fields are not canonical state."));
			}
			const uint64 Bits = IsUnsignedNumeric(*Numeric)
				? Numeric->GetUnsignedIntPropertyValue(ValuePtr)
				: static_cast<uint64>(
					Numeric->GetSignedIntPropertyValue(ValuePtr));
			return Context.Write(
				Writer, Writer.WriteUInt64(Bits), FieldPath);
		}
		if (const FNameProperty* Name =
			CastField<FNameProperty>(&Property))
		{
			return Context.WriteName(
				Writer, Name->GetPropertyValue(ValuePtr), FieldPath);
		}
		if (const FStrProperty* String =
			CastField<FStrProperty>(&Property))
		{
			return Context.WriteString(
				Writer, String->GetPropertyValue(ValuePtr), FieldPath);
		}
		if (Property.IsA<FTextProperty>())
		{
			return Context.Fail(
				FieldPath,
				TEXT("FText is presentation state and is unsupported inside canonical containers."));
		}
		if (const FStructProperty* StructProperty =
			CastField<FStructProperty>(&Property))
		{
			const UScriptStruct* Struct = StructProperty->Struct;
			if (!Struct)
			{
				return Context.Fail(
					FieldPath,
					TEXT("Struct property has no reflected type."));
			}
			if (Struct == FInstancedStruct::StaticStruct())
			{
				const FInstancedStruct& Dynamic =
					*static_cast<const FInstancedStruct*>(ValuePtr);
				if (!Context.Write(
					Writer,
					Writer.WriteBool(Dynamic.IsValid()),
					FieldPath))
				{
					return false;
				}
				if (!Dynamic.IsValid())
				{
					return true;
				}
				if (!Dynamic.GetScriptStruct() || !Dynamic.GetMemory())
				{
					return Context.Fail(
						FieldPath,
						TEXT("FInstancedStruct is valid without a type or value."));
				}
				FGuid DynamicSchemaDigest;
				return ResolveSchemaDigest(
						Dynamic.GetScriptStruct(),
						Context,
						FieldPath + TEXT(".<dynamic-schema>"),
						DynamicSchemaDigest)
					&& Context.Write(
						Writer,
						Writer.WriteGuid(DynamicSchemaDigest),
						FieldPath)
					&& WriteReflectedValue(
					Dynamic.GetScriptStruct(),
					Dynamic.GetMemory(),
					Context,
					Writer,
					Depth + 1,
					FieldPath);
			}
			if (Struct == FGameplayTag::StaticStruct())
			{
				const FGameplayTag& Tag =
					*static_cast<const FGameplayTag*>(ValuePtr);
				return Context.WriteName(
					Writer, Tag.GetTagName(), FieldPath);
			}
			if (Struct == FGameplayTagContainer::StaticStruct())
			{
				const FGameplayTagContainer& Container =
					*static_cast<const FGameplayTagContainer*>(ValuePtr);
				const TArray<FGameplayTag>& Tags =
					Container.GetGameplayTagArray();
				if (!Context.AddElements(Tags.Num(), FieldPath))
				{
					return false;
				}
				TArray<FName> TagNames;
				TagNames.Reserve(Tags.Num());
				for (const FGameplayTag& Tag : Tags)
				{
					TagNames.Add(Tag.GetTagName());
				}
				TagNames.Sort([](const FName A, const FName B)
				{
					return CanonicalNameText(A).Compare(
						CanonicalNameText(B),
						ESearchCase::CaseSensitive) < 0;
				});
				if (!Context.Write(
					Writer,
					Writer.WriteUInt32(
						static_cast<uint32>(TagNames.Num())),
					FieldPath))
				{
					return false;
				}
				for (const FName TagName : TagNames)
				{
					if (!Context.WriteName(
						Writer, TagName, FieldPath))
					{
						return false;
					}
				}
				return true;
			}
			if (Struct == FSoftObjectPath::StaticStruct()
				|| Struct == FSoftClassPath::StaticStruct())
			{
				return Context.WriteString(
					Writer,
					static_cast<const FSoftObjectPath*>(ValuePtr)
						->ToString(),
					FieldPath);
			}
			if (Struct == FTopLevelAssetPath::StaticStruct())
			{
				return Context.WriteString(
					Writer,
					static_cast<const FTopLevelAssetPath*>(ValuePtr)
						->ToString(),
					FieldPath);
			}
			return WriteReflectedValue(
				Struct,
				ValuePtr,
				Context,
				Writer,
				Depth + 1,
				FieldPath);
		}
		if (const FSoftObjectProperty* SoftObject =
			CastField<FSoftObjectProperty>(&Property))
		{
			const FSoftObjectPtr Soft =
				SoftObject->GetPropertyValue(ValuePtr);
			const FSoftObjectPath Path = Soft.ToSoftObjectPath();
			return Context.Write(
					Writer, Writer.WriteBool(!Path.IsNull()), FieldPath)
				&& (Path.IsNull()
					|| Context.WriteString(
						Writer, Path.ToString(), FieldPath));
		}
		if (Property.IsA<FWeakObjectProperty>()
			|| Property.IsA<FLazyObjectProperty>())
		{
			return Context.Fail(
				FieldPath,
				TEXT("Weak and lazy runtime object references are not canonical state."));
		}
		if (const FObjectPropertyBase* ObjectProperty =
			CastField<FObjectPropertyBase>(&Property))
		{
			const UObject* Object =
				ObjectProperty->GetObjectPropertyValue(ValuePtr);
			if (!Context.Write(
				Writer, Writer.WriteBool(Object != nullptr), FieldPath))
			{
				return false;
			}
			if (!Object)
			{
				return true;
			}

			const bool bInstanced = Property.HasAnyPropertyFlags(
				CPF_InstancedReference | CPF_ContainsInstancedReference);
			if (bInstanced)
			{
				if (Context.UnorderedContainerDepth > 0)
				{
					return Context.Fail(
						FieldPath,
						TEXT("Instanced UObject references inside unordered containers are unsupported because encounter order is not canonical."));
				}
				if (Context.ObjectStack.Contains(Object))
				{
					return Context.Fail(
						FieldPath,
						TEXT("Instanced UObject reference cycle detected."));
				}
				if (const uint32* ExistingID =
					Context.ObjectEncounterIDs.Find(Object))
				{
					return Context.Write(
							Writer, Writer.WriteUInt8(2), FieldPath)
						&& Context.Write(
							Writer,
							Writer.WriteUInt32(*ExistingID),
							FieldPath);
				}
				if (Context.InstancedObjects
					>= Context.Limits.MaxInstancedObjects)
				{
					return Context.Fail(
						FieldPath,
						TEXT("Instanced UObject limit exceeded."));
				}
				++Context.InstancedObjects;
				const uint32 EncounterID =
					static_cast<uint32>(
						Context.ObjectEncounterIDs.Num() + 1);
				Context.ObjectEncounterIDs.Add(Object, EncounterID);
				FGuid DynamicSchemaDigest;
				if (!ResolveSchemaDigest(
						Object->GetClass(),
						Context,
						FieldPath + TEXT(".<dynamic-schema>"),
						DynamicSchemaDigest)
					|| !Context.Write(
						Writer, Writer.WriteUInt8(1), FieldPath)
					|| !Context.Write(
						Writer,
						Writer.WriteUInt32(EncounterID),
						FieldPath)
					|| !Context.Write(
						Writer,
						Writer.WriteGuid(DynamicSchemaDigest),
						FieldPath))
				{
					return false;
				}
				Context.ObjectStack.Add(Object);
				const bool bWrote =
					WriteReflectedValue(
						Object->GetClass(),
						Object,
						Context,
						Writer,
						Depth + 1,
						FieldPath);
				Context.ObjectStack.Pop();
				return bWrote;
			}

			if (!Object->IsA<UClass>()
				&& !Object->IsAsset()
				&& !Object->HasAnyFlags(RF_ClassDefaultObject))
			{
				return Context.Fail(
					FieldPath,
					FString::Printf(
						TEXT("Runtime UObject reference '%s' is not a stable asset, class, CDO, or instanced subobject."),
						*Object->GetPathName()));
			}
			return Context.WriteString(
				Writer, Object->GetPathName(), FieldPath);
		}

		return Context.Fail(
			FieldPath,
			FString::Printf(
				TEXT("Unsupported reflected property kind '%s'."),
				*Property.GetClass()->GetName()));
	}

	bool WriteReflectedValue(
		const UStruct* Type,
		const void* Memory,
		FProjectionContext& Context,
		FSeinCanonicalDigestWriter& Writer,
		const int32 Depth,
		const FString& FieldPath)
	{
		if (!Type || !Memory)
		{
			return Context.Fail(
				FieldPath,
				TEXT("Reflected state type or value memory is null."));
		}
		if (!Context.CheckDepth(Depth, FieldPath)
			|| !Context.WriteString(
				Writer, Type->GetPathName(), FieldPath))
		{
			return false;
		}

		TArray<const FProperty*> Properties;
		GatherCanonicalProperties(Type, Properties);
		if (!Context.Write(
			Writer,
			Writer.WriteUInt32(static_cast<uint32>(Properties.Num())),
			FieldPath))
		{
			return false;
		}
		for (const FProperty* Property : Properties)
		{
			const FString PropertyPath =
				ChildPath(FieldPath, *Property);
			if (!Context.WriteString(
					Writer, PropertyOwnerPath(*Property), PropertyPath)
				|| !Context.WriteName(
					Writer, Property->GetFName(), PropertyPath)
				|| !Context.Write(
					Writer,
					Writer.WriteInt32(Property->ArrayDim),
					PropertyPath))
			{
				return false;
			}
			for (int32 ArrayIndex = 0;
				ArrayIndex < Property->ArrayDim;
				++ArrayIndex)
			{
				const FString ValuePath = Property->ArrayDim > 1
					? FString::Printf(
						TEXT("%s[%d]"), *PropertyPath, ArrayIndex)
					: PropertyPath;
				const void* ValuePtr =
					Property->ContainerPtrToValuePtr<void>(
						Memory, ArrayIndex);
				if (!WritePropertyValue(
					*Property,
					ValuePtr,
					Context,
					Writer,
					Depth + 1,
					ValuePath))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool FinishRoot(
		FSeinCanonicalDigestWriter& Writer,
		FProjectionContext& Context,
		FGuid& OutDigest,
		FString& OutError)
	{
		if (!Context.Error.IsEmpty())
		{
			OutError = MoveTemp(Context.Error);
			OutDigest.Invalidate();
			return false;
		}
		if (!Writer.Finalize(OutDigest, OutError))
		{
			OutDigest.Invalidate();
			return false;
		}
		return true;
	}
}

bool FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
	const UStruct* Type,
	const FSeinCanonicalReflectedStateLimits& Limits,
	FGuid& OutDigest,
	FString& OutError)
{
	OutDigest.Invalidate();
	OutError.Reset();
	FProjectionContext Context(Limits);
	if (!ResolveSchemaDigest(
		Type,
		Context,
		Type ? Type->GetPathName() : TEXT("<null>"),
		OutDigest))
	{
		OutError = MoveTemp(Context.Error);
		return false;
	}
	return true;
}

bool FSeinCanonicalReflectedStateDigest::ComputeStructValueDigest(
	const UScriptStruct* Type,
	const void* StructMemory,
	const FGuid& SchemaDigest,
	const FSeinCanonicalReflectedStateLimits& Limits,
	FGuid& OutDigest,
	FString& OutError)
{
	OutDigest.Invalidate();
	OutError.Reset();
	if (!Type || !StructMemory || !SchemaDigest.IsValid())
	{
		OutError =
			TEXT("Canonical reflected struct state requires a type, value, and valid schema digest.");
		return false;
	}

	FProjectionContext Context(Limits);
	Context.SchemaDigests.Add(Type, SchemaDigest);
	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.ReflectedState.StructValue"),
		ReflectedValueFormatVersion);
	if (!Context.Write(
			Writer, Writer.WriteGuid(SchemaDigest), Type->GetPathName())
		|| !WriteReflectedValue(
			Type,
			StructMemory,
			Context,
			Writer,
			0,
			Type->GetPathName()))
	{
		OutError = Context.Error.IsEmpty()
			? Writer.GetError()
			: MoveTemp(Context.Error);
		return false;
	}
	return FinishRoot(Writer, Context, OutDigest, OutError);
}

bool FSeinCanonicalReflectedStateDigest::ComputeObjectValueDigest(
	const UObject* Object,
	const FGuid& SchemaDigest,
	const FSeinCanonicalReflectedStateLimits& Limits,
	FGuid& OutDigest,
	FString& OutError)
{
	OutDigest.Invalidate();
	OutError.Reset();
	if (!Object || !Object->GetClass() || !SchemaDigest.IsValid())
	{
		OutError =
			TEXT("Canonical reflected UObject state requires an object, class, and valid schema digest.");
		return false;
	}

	FProjectionContext Context(Limits);
	Context.SchemaDigests.Add(Object->GetClass(), SchemaDigest);
	Context.ObjectStack.Add(Object);
	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinARTS.ReflectedState.ObjectValue"),
		ReflectedValueFormatVersion);
	if (!Context.Write(
			Writer,
			Writer.WriteGuid(SchemaDigest),
			Object->GetClass()->GetPathName())
		|| !WriteReflectedValue(
			Object->GetClass(),
			Object,
			Context,
			Writer,
			0,
			Object->GetClass()->GetPathName()))
	{
		OutError = Context.Error.IsEmpty()
			? Writer.GetError()
			: MoveTemp(Context.Error);
		return false;
	}
	Context.ObjectStack.Pop();
	return FinishRoot(Writer, Context, OutDigest, OutError);
}
