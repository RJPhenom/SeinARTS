/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateRecipeRegistry.cpp
 */

#include "Simulation/SeinCanonicalStateRecipeRegistry.h"

#include "Input/SeinCommandSchemaRegistry.h"
#include "Misc/ScopeLock.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "Serialization/SeinCanonicalStateCodec.h"
#include "UObject/Class.h"
#include "UObject/GCObject.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCanonicalStateRecipe, Log, All);

namespace
{
	constexpr uint32 RecipeRegistryFormatVersion = 1;

	struct FRecipeClaim
	{
		uint64 Token = 0;
		FString CanonicalOwnerID;
	};

	struct FRegisteredRecipe
	{
		FString CanonicalContributorID;
		FSeinCanonicalStateRecipeDescriptor Descriptor;
		TArray<FRecipeClaim> Claims;
	};

	TArray<FRegisteredRecipe>& Registry()
	{
		static TArray<FRegisteredRecipe> Value;
		return Value;
	}

	FCriticalSection& RegistryMutex()
	{
		static FCriticalSection Value;
		return Value;
	}

	uint64& NextToken()
	{
		static uint64 Value = 1;
		return Value;
	}

	TSet<uint64>& FailedClaimTokens()
	{
		static TSet<uint64> Value;
		return Value;
	}

	bool& HasUnleasedFailure()
	{
		static bool Value = false;
		return Value;
	}

	int32& InvocationDepth()
	{
		static int32 Value = 0;
		return Value;
	}

	bool IsInvocationActive()
	{
		check(IsInGameThread());
		return InvocationDepth() != 0;
	}

	class FRecipeInvocationScope
	{
	public:
		FRecipeInvocationScope()
		{
			check(IsInGameThread());
			check(InvocationDepth() == 0);
			++InvocationDepth();
		}

		~FRecipeInvocationScope()
		{
			check(IsInGameThread());
			check(InvocationDepth() == 1);
			--InvocationDepth();
		}

		FRecipeInvocationScope(const FRecipeInvocationScope&) = delete;
		FRecipeInvocationScope& operator=(
			const FRecipeInvocationScope&) = delete;
	};

	template<typename ReflectedStructType>
	void AddStackStructReferences(
		FReferenceCollector& Collector,
		const ReflectedStructType& Value)
	{
		Collector.AddPropertyReferencesWithStructARO(
			ReflectedStructType::StaticStruct(),
			const_cast<ReflectedStructType*>(&Value));
	}

	void AddDeclarationReferences(
		FReferenceCollector& Collector,
		const FSeinCanonicalStateRecipeDeclaration& Declaration)
	{
		for (const FSeinCanonicalStateRecipeSlotDeclaration& Slot :
			Declaration.Slots)
		{
			AddStackStructReferences(Collector, Slot);
		}
	}

	void AddMaterializationReferences(
		FReferenceCollector& Collector,
		const FSeinCanonicalStateRecipeMaterialization&
			Materialization)
	{
		for (const FSeinCanonicalStateRecipeInitialValue& Value :
			Materialization.Values)
		{
			AddStackStructReferences(Collector, Value);
		}
	}

	/**
	 * Stack-owned reflected values are not visible to Unreal GC by themselves.
	 * Keep the complete declaration or materialization transaction reachable
	 * if a native or Blueprint recipe synchronously triggers collection.
	 */
	class FRecipeExecutionReferenceGuard final : public FGCObject
	{
	public:
		FRecipeExecutionReferenceGuard(
			const FSeinMatchSettings& InMatchSettings,
			const TArray<FSeinCanonicalStateRecipeSlotDeclaration>&
				InRawSlots,
			const TArray<FSeinCanonicalStateRecipeDeclaration>&
				InCandidateDeclarations)
			: FGCObject(FGCObject::EFlags::RegisterLater)
			, MatchSettings(InMatchSettings)
			, RawSlots(&InRawSlots)
			, CandidateDeclarations(&InCandidateDeclarations)
			, ReferencerName(
				TEXT("SeinCanonicalStateRecipeDeclaration"))
		{
			RegisterGCObject();
		}

		FRecipeExecutionReferenceGuard(
			const FSeinMatchSettings& InMatchSettings,
			TConstArrayView<
				FSeinCanonicalStateRecipeDeclaration>
				InDeclarations,
			const FSeinCanonicalStateRecipeDeclaration&
				InValidatedDeclaration,
			const TArray<FSeinCanonicalStateRecipeInitialValue>&
				InRawValues,
			const TArray<
				FSeinCanonicalStateRecipeMaterialization>&
				InCandidate)
			: FGCObject(FGCObject::EFlags::RegisterLater)
			, MatchSettings(InMatchSettings)
			, Declarations(InDeclarations)
			, ValidatedDeclaration(&InValidatedDeclaration)
			, RawValues(&InRawValues)
			, CandidateMaterializations(&InCandidate)
			, ReferencerName(
				TEXT("SeinCanonicalStateRecipeMaterialization"))
		{
			RegisterGCObject();
		}

		void SetRecipe(
			UClass* InRecipeClass,
			const USeinCanonicalStateRecipe* InRecipe)
		{
			RecipeClass = InRecipeClass;
			Recipe =
				const_cast<USeinCanonicalStateRecipe*>(
					InRecipe);
		}

		void ClearRecipe()
		{
			RecipeClass = nullptr;
			Recipe = nullptr;
		}

		virtual void AddReferencedObjects(
			FReferenceCollector& Collector) override
		{
			Collector.AddReferencedObject(RecipeClass);
			Collector.AddReferencedObject(Recipe);
			AddStackStructReferences(
				Collector, MatchSettings);

			if (RawSlots)
			{
				for (const FSeinCanonicalStateRecipeSlotDeclaration&
					Slot : *RawSlots)
				{
					AddStackStructReferences(Collector, Slot);
				}
			}
			for (const FSeinCanonicalStateRecipeDeclaration&
				Declaration : Declarations)
			{
				AddDeclarationReferences(
					Collector, Declaration);
			}
			if (CandidateDeclarations)
			{
				for (const FSeinCanonicalStateRecipeDeclaration&
					Declaration : *CandidateDeclarations)
				{
					AddDeclarationReferences(
						Collector, Declaration);
				}
			}
			if (ValidatedDeclaration)
			{
				AddDeclarationReferences(
					Collector, *ValidatedDeclaration);
			}
			if (RawValues)
			{
				for (const FSeinCanonicalStateRecipeInitialValue&
					Value : *RawValues)
				{
					AddStackStructReferences(Collector, Value);
				}
			}
			if (CandidateMaterializations)
			{
				for (const FSeinCanonicalStateRecipeMaterialization&
					Materialization : *CandidateMaterializations)
				{
					AddMaterializationReferences(
						Collector, Materialization);
				}
			}
		}

		virtual FString GetReferencerName() const override
		{
			return ReferencerName;
		}

	private:
		const FSeinMatchSettings& MatchSettings;
		TConstArrayView<FSeinCanonicalStateRecipeDeclaration>
			Declarations;
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>*
			RawSlots = nullptr;
		const TArray<FSeinCanonicalStateRecipeDeclaration>*
			CandidateDeclarations = nullptr;
		const FSeinCanonicalStateRecipeDeclaration*
			ValidatedDeclaration = nullptr;
		const TArray<FSeinCanonicalStateRecipeInitialValue>*
			RawValues = nullptr;
		const TArray<FSeinCanonicalStateRecipeMaterialization>*
			CandidateMaterializations = nullptr;
		const TCHAR* ReferencerName = nullptr;
		TObjectPtr<UClass> RecipeClass;
		TObjectPtr<USeinCanonicalStateRecipe> Recipe;
	};

	void SetError(FString* OutError, FString Error)
	{
		if (OutError)
		{
			*OutError = MoveTemp(Error);
		}
	}

	void ResetError(FString* OutError)
	{
		if (OutError)
		{
			OutError->Reset();
		}
	}

	bool IsStableASCIIIdentifier(const FString& Value)
	{
		if (Value.IsEmpty()
			|| Value.Len()
				> FSeinCanonicalStateRecipeRegistry::MaxContributorIDChars)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			const bool bValid =
				(Character >= TEXT('a') && Character <= TEXT('z'))
				|| (Character >= TEXT('0') && Character <= TEXT('9'))
				|| Character == TEXT('.')
				|| Character == TEXT('_')
				|| Character == TEXT('-');
			if (!bValid)
			{
				return false;
			}
		}
		return true;
	}

	bool IsUnstableGeneratedName(const FString& Name)
	{
		return Name.StartsWith(TEXT("REINST_"))
			|| Name.StartsWith(TEXT("SKEL_"))
			|| Name.StartsWith(TEXT("TRASHCLASS_"));
	}

	void AppendFramed(FString& Out, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		Out += FString::Printf(TEXT("%d:"), Utf8.Length());
		Out += Value;
		Out += TEXT("\n");
	}

	bool DescriptorsMatch(
		const FSeinCanonicalStateRecipeDescriptor& A,
		const FSeinCanonicalStateRecipeDescriptor& B)
	{
		return FSeinCanonicalStateRecipeRegistry::CanonicalContributorID(
				A.StableContributorID)
			== FSeinCanonicalStateRecipeRegistry::CanonicalContributorID(
				B.StableContributorID)
			&& A.ContributorSchemaVersion == B.ContributorSchemaVersion
			&& A.ImplementationRevision == B.ImplementationRevision
			&& A.RecipeClassPath == B.RecipeClassPath;
	}

	bool ResolveRecipeClass(
		const FString& ExactClassPath,
		UClass*& OutClass,
		const USeinCanonicalStateRecipe*& OutRecipe,
		FSeinCanonicalStateRecipeDescriptor& OutDescriptor,
		FString& OutError)
	{
		OutClass = nullptr;
		OutRecipe = nullptr;
		OutDescriptor = {};
		if (ExactClassPath.IsEmpty()
			|| ExactClassPath.Len()
				> FSeinCanonicalStateRecipeRegistry::
					MaxRecipeClassPathChars)
		{
			OutError = TEXT("Canonical-state recipe class path is invalid.");
			return false;
		}

		UClass* RecipeClass =
			FindObject<UClass>(nullptr, *ExactClassPath);
		if (!RecipeClass)
		{
			RecipeClass =
				FSoftClassPath(ExactClassPath)
					.TryLoadClass<USeinCanonicalStateRecipe>();
		}
		if (!RecipeClass
			|| RecipeClass->GetPathName() != ExactClassPath
			|| !RecipeClass->IsChildOf(
				USeinCanonicalStateRecipe::StaticClass())
			|| RecipeClass->HasAnyClassFlags(
				CLASS_Abstract
				| CLASS_Deprecated
				| CLASS_NewerVersionExists)
			|| !RecipeClass->HasAnyClassFlags(CLASS_Const)
			|| IsUnstableGeneratedName(RecipeClass->GetName()))
		{
			OutError = FString::Printf(
				TEXT("Recipe class '%s' must resolve exactly to a concrete, current, const USeinCanonicalStateRecipe class."),
				*ExactClassPath);
			return false;
		}

		const USeinCanonicalStateRecipe* Recipe =
			Cast<USeinCanonicalStateRecipe>(
				RecipeClass->GetDefaultObject());
		if (!Recipe)
		{
			OutError = FString::Printf(
				TEXT("Recipe class '%s' has no valid class default object."),
				*ExactClassPath);
			return false;
		}

		const FString CanonicalID =
			FSeinCanonicalStateRecipeRegistry::CanonicalContributorID(
				Recipe->StableContributorID);
		if (CanonicalID.IsEmpty()
			|| Recipe->ContributorSchemaVersion <= 0
			|| Recipe->ImplementationRevision <= 0
			|| Recipe->DefaultSlotDeclarations.Num()
				> FSeinCanonicalStateRecipeRegistry::MaxSlotsPerRecipe)
		{
			OutError = FString::Printf(
				TEXT("Recipe class '%s' requires a stable contributor ID, positive schema/implementation revisions, and at most %d default slots."),
				*ExactClassPath,
				FSeinCanonicalStateRecipeRegistry::MaxSlotsPerRecipe);
			return false;
		}

		OutDescriptor.StableContributorID =
			Recipe->StableContributorID;
		OutDescriptor.ContributorSchemaVersion =
			static_cast<uint32>(
				Recipe->ContributorSchemaVersion);
		OutDescriptor.ImplementationRevision =
			static_cast<uint32>(
				Recipe->ImplementationRevision);
		OutDescriptor.RecipeClassPath = ExactClassPath;
		OutClass = RecipeClass;
		OutRecipe = Recipe;
		return true;
	}

	bool IsExactProviderLeaseAvailable(
		const FSeinCanonicalStateRecipeDescriptor& Descriptor,
		uint64 Token)
	{
		const FString CanonicalID =
			FSeinCanonicalStateRecipeRegistry::CanonicalContributorID(
				Descriptor.StableContributorID);
		FScopeLock Lock(&RegistryMutex());
		const FRegisteredRecipe* Registered =
			Registry().FindByPredicate(
				[&CanonicalID](const FRegisteredRecipe& Candidate)
				{
					return Candidate.CanonicalContributorID
						== CanonicalID;
				});
		return Registered
			&& DescriptorsMatch(
				Registered->Descriptor, Descriptor)
			&& Registered->Claims.ContainsByPredicate(
				[Token](const FRecipeClaim& Claim)
				{
					return Claim.Token == Token;
				});
	}

	bool ResolveFrozenRecipe(
		const FSeinCanonicalStateRecipeDescriptor& FrozenDescriptor,
		uint64 ProviderToken,
		TStrongObjectPtr<UClass>& OutClassRoot,
		const USeinCanonicalStateRecipe*& OutRecipe,
		FString& OutError)
	{
		OutClassRoot.Reset();
		OutRecipe = nullptr;
		if (!IsExactProviderLeaseAvailable(
			FrozenDescriptor, ProviderToken))
		{
			OutError = FString::Printf(
				TEXT("Recipe provider '%s' was unregistered or replaced after freeze."),
				*FSeinCanonicalStateRecipeRegistry::
					CanonicalContributorID(
						FrozenDescriptor.StableContributorID));
			return false;
		}

		UClass* RecipeClass = nullptr;
		FSeinCanonicalStateRecipeDescriptor CurrentDescriptor;
		if (!ResolveRecipeClass(
				FrozenDescriptor.RecipeClassPath,
				RecipeClass,
				OutRecipe,
				CurrentDescriptor,
				OutError)
			|| !DescriptorsMatch(
				FrozenDescriptor, CurrentDescriptor))
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Recipe '%s' no longer matches its frozen identity."),
					*FSeinCanonicalStateRecipeRegistry::
						CanonicalContributorID(
							FrozenDescriptor.StableContributorID));
			}
			OutRecipe = nullptr;
			return false;
		}

		OutClassRoot.Reset(RecipeClass);
		return true;
	}

	FSeinStructWireLimits BuildWireLimits(
		const FSeinCanonicalStateLimits& StateLimits)
	{
		FSeinStructWireLimits Limits;
		Limits.MaxBytes = StateLimits.MaxEncodedBytes;
		Limits.MaxAggregateElements =
			StateLimits.MaxAggregateElements;
		Limits.MaxStringBytes = FMath::Min(
			StateLimits.MaxEncodedBytes, 1024 * 1024);
		Limits.MaxRecursionDepth =
			StateLimits.MaxRecursionDepth;
		Limits.MaxNativeAllocationBytes =
			StateLimits.MaxEncodedBytes;
		return Limits;
	}

	bool CanonicalizeSlot(
		const FSeinCanonicalStateRecipeSlotDeclaration& Input,
		FSeinCanonicalStateRecipeSlotDeclaration& OutSlot,
		FString& OutCanonicalDescriptor,
		FString& OutCanonicalKey,
		FString& OutError)
	{
		OutSlot = Input;
		OutCanonicalDescriptor.Reset();
		OutCanonicalKey.Reset();
		FSeinCanonicalStateValueSlotDefinition& Definition =
			OutSlot.Definition;

		const FString Domain =
			Definition.Key.StableDomainId.GetPlainNameString();
		const FString Contributor =
			Definition.Key.StableContributorId.GetPlainNameString();
		OutCanonicalKey =
			FSeinCanonicalStateRegistry::CanonicalKey(
				Definition.Key);
		if (OutCanonicalKey.IsEmpty()
			|| Domain.Len()
				> FSeinCanonicalStateRecipeRegistry::
					MaxContributorIDChars
			|| Contributor.Len()
				> FSeinCanonicalStateRecipeRegistry::
					MaxContributorIDChars
			|| !OutSlot.DefaultValue.IsValid()
			|| Definition.SchemaVersion <= 0
			|| Definition.ImplementationRevision <= 0)
		{
			OutError =
				TEXT("Slots require bounded stable IDs, a concrete default value, and positive revisions.");
			return false;
		}

		if (Definition.DynamicPayloadSchemas.Num()
				> FSeinCanonicalStateRecipeRegistry::
					MaxDynamicSchemasPerSlot
			|| Definition.AllowedNames.Num()
				> FSeinCanonicalStateRecipeRegistry::
					MaxAllowedNamesPerSlot
			|| Definition.Limits.MaxRecursionDepth <= 0
			|| Definition.Limits.MaxRecursionDepth
				> FSeinCanonicalStateRecipeRegistry::
					MaxSlotRecursionDepth
			|| Definition.Limits.MaxEncodedBytes <= 0
			|| Definition.Limits.MaxEncodedBytes
				> FSeinCanonicalStateRecipeRegistry::
					MaxSlotEncodedBytes
			|| Definition.Limits.MaxAggregateElements <= 0
			|| Definition.Limits.MaxAggregateElements
				> FSeinCanonicalStateRecipeRegistry::
					MaxSlotAggregateElements)
		{
			OutError =
				TEXT("Slot schema catalogs or defensive wire limits exceed recipe bounds.");
			return false;
		}

		Definition.DynamicPayloadSchemas.Sort(
			[](const FInstancedStruct& A, const FInstancedStruct& B)
			{
				if (!A.IsValid())
				{
					return B.IsValid();
				}
				if (!B.IsValid())
				{
					return false;
				}
				return A.GetScriptStruct()->GetPathName()
					< B.GetScriptStruct()->GetPathName();
			});

		TArray<const UScriptStruct*> DynamicTypes;
		DynamicTypes.Reserve(
			Definition.DynamicPayloadSchemas.Num());
		FString PreviousDynamicPath;
		for (const FInstancedStruct& DynamicSchema :
			Definition.DynamicPayloadSchemas)
		{
			if (!DynamicSchema.IsValid())
			{
				OutError =
					TEXT("Dynamic payload schema entries must contain concrete values.");
				return false;
			}
			const UScriptStruct* DynamicType =
				DynamicSchema.GetScriptStruct();
			const FString DynamicPath =
				DynamicType->GetPathName();
			if (DynamicPath.IsEmpty()
				|| DynamicPath.Len()
					> FSeinCanonicalStateRecipeRegistry::
						MaxRecipeClassPathChars
				|| DynamicPath == PreviousDynamicPath
				|| IsUnstableGeneratedName(
					DynamicType->GetName()))
			{
				OutError =
					TEXT("Dynamic payload schema types require unique stable paths.");
				return false;
			}
			PreviousDynamicPath = DynamicPath;
			DynamicTypes.Add(DynamicType);
		}

		TArray<FName> CanonicalNames;
		FString IgnoredNameManifest;
		SeinBuildCanonicalWireNameCatalog(
			Definition.AllowedNames,
			CanonicalNames,
			IgnoredNameManifest);
		Definition.AllowedNames = MoveTemp(CanonicalNames);

		const UScriptStruct* RootType =
			OutSlot.DefaultValue.GetScriptStruct();
		if (!RootType
			|| RootType->GetPathName().IsEmpty()
			|| RootType->GetPathName().Len()
				> FSeinCanonicalStateRecipeRegistry::
					MaxRecipeClassPathChars
			|| IsUnstableGeneratedName(RootType->GetName()))
		{
			OutError =
				TEXT("Slot root payload type requires a stable bounded path.");
			return false;
		}

		FSeinCanonicalStateDescriptor Descriptor;
		Descriptor.Key = Definition.Key;
		Descriptor.SchemaVersion =
			static_cast<uint32>(Definition.SchemaVersion);
		Descriptor.ImplementationRevision =
			static_cast<uint32>(
				Definition.ImplementationRevision);
		Descriptor.Role =
			ESeinCanonicalStateRole::Authoritative;
		Descriptor.PayloadStruct = RootType;
		Descriptor.DynamicPayloadStructs =
			MoveTemp(DynamicTypes);
		Descriptor.AllowedNames =
			Definition.AllowedNames;
		Descriptor.Limits = Definition.Limits;

		FGuid DescriptorDigest;
		return FSeinCanonicalStateRegistry::
			BuildDescriptorIdentity(
				Descriptor,
				OutCanonicalDescriptor,
				DescriptorDigest,
				OutError);
	}

	struct FCanonicalSlot
	{
		FString CanonicalKey;
		FString CanonicalDescriptor;
		FSeinCanonicalStateRecipeSlotDeclaration Slot;
	};

	bool BuildCanonicalDeclaration(
		const FSeinCanonicalStateRecipeDescriptor& Recipe,
		TConstArrayView<FSeinCanonicalStateRecipeSlotDeclaration> RawSlots,
		TSet<FString>& InOutGlobalKeys,
		int32& InOutTotalSlots,
		FSeinCanonicalStateRecipeDeclaration& OutDeclaration,
		FString& OutError)
	{
		OutDeclaration = {};
		if (RawSlots.Num()
				> FSeinCanonicalStateRecipeRegistry::
					MaxSlotsPerRecipe
			|| RawSlots.Num()
				> FSeinCanonicalStateRecipeRegistry::
					MaxTotalSlots - InOutTotalSlots)
		{
			OutError = FString::Printf(
				TEXT("Recipe '%s' exceeded bounded slot contribution counts."),
				*FSeinCanonicalStateRecipeRegistry::
					CanonicalContributorID(
						Recipe.StableContributorID));
			return false;
		}

		TArray<FCanonicalSlot> CanonicalSlots;
		CanonicalSlots.Reserve(RawSlots.Num());
		for (int32 Index = 0; Index < RawSlots.Num(); ++Index)
		{
			FCanonicalSlot& Candidate =
				CanonicalSlots.AddDefaulted_GetRef();
			if (!CanonicalizeSlot(
				RawSlots[Index],
				Candidate.Slot,
				Candidate.CanonicalDescriptor,
				Candidate.CanonicalKey,
				OutError))
			{
				OutError = FString::Printf(
					TEXT("%s slot %d: %s"),
					*FSeinCanonicalStateRecipeRegistry::
						CanonicalContributorID(
							Recipe.StableContributorID),
					Index,
					*OutError);
				return false;
			}
		}
		CanonicalSlots.Sort(
			[](const FCanonicalSlot& A,
				const FCanonicalSlot& B)
			{
				return A.CanonicalKey < B.CanonicalKey;
			});

		for (int32 Index = 0;
			Index < CanonicalSlots.Num();
			++Index)
		{
			const FString& Key =
				CanonicalSlots[Index].CanonicalKey;
			if ((Index > 0
					&& CanonicalSlots[Index - 1].CanonicalKey
						== Key)
				|| InOutGlobalKeys.Contains(Key))
			{
				OutError = FString::Printf(
					TEXT("Canonical state slot key '%s' is declared more than once."),
					*Key);
				return false;
			}
		}

		OutDeclaration.Recipe = Recipe;
		OutDeclaration.Slots.Reserve(
			CanonicalSlots.Num());
		OutDeclaration.CanonicalSlotDescriptors.Reserve(
			CanonicalSlots.Num());
		for (FCanonicalSlot& Canonical :
			CanonicalSlots)
		{
			InOutGlobalKeys.Add(Canonical.CanonicalKey);
			OutDeclaration.Slots.Add(
				MoveTemp(Canonical.Slot));
			OutDeclaration.CanonicalSlotDescriptors.Add(
				MoveTemp(Canonical.CanonicalDescriptor));
		}
		InOutTotalSlots += CanonicalSlots.Num();
		return true;
	}

	bool ValidateMaterializedValue(
		const FSeinCanonicalStateRecipeSlotDeclaration& Slot,
		const FInstancedStruct& Value,
		FString& OutError)
	{
		if (!Value.IsValid()
			|| Value.GetScriptStruct()
				!= Slot.DefaultValue.GetScriptStruct())
		{
			OutError =
				TEXT("Materialized value does not match the declared root payload type.");
			return false;
		}

		TArray<const UScriptStruct*> DynamicTypes;
		DynamicTypes.Reserve(
			Slot.Definition.DynamicPayloadSchemas.Num());
		for (const FInstancedStruct& DynamicSchema :
			Slot.Definition.DynamicPayloadSchemas)
		{
			if (!DynamicSchema.IsValid())
			{
				OutError =
					TEXT("Materialized value declaration contains an invalid dynamic schema.");
				return false;
			}
			DynamicTypes.Add(
				DynamicSchema.GetScriptStruct());
		}

		TArray<uint8> Encoded;
		return FSeinCanonicalStateCodec::Encode(
			Value.GetScriptStruct(),
			Value.GetMemory(),
			{ DynamicTypes, Slot.Definition.AllowedNames },
			BuildWireLimits(Slot.Definition.Limits),
			Encoded,
			OutError);
	}

	bool BuildRegistryIdentity(
		TConstArrayView<FSeinCanonicalStateRecipeDescriptor> Recipes,
		FString& OutManifest,
		FGuid& OutDigest,
		FString& OutError)
	{
		OutManifest =
			TEXT("SeinARTS.CanonicalState.RecipeRegistry\n");
		AppendFramed(
			OutManifest,
			LexToString(RecipeRegistryFormatVersion));
		AppendFramed(
			OutManifest,
			LexToString(Recipes.Num()));

		FSeinCanonicalDigestWriter Writer(
			TEXT("SeinARTS.CanonicalState.RecipeRegistry"),
			RecipeRegistryFormatVersion);
		if (!Writer.WriteUInt32(
			static_cast<uint32>(Recipes.Num())))
		{
			OutError = Writer.GetError();
			return false;
		}

		for (const FSeinCanonicalStateRecipeDescriptor& Recipe :
			Recipes)
		{
			const FString CanonicalID =
				FSeinCanonicalStateRecipeRegistry::
					CanonicalContributorID(
						Recipe.StableContributorID);
			AppendFramed(OutManifest, CanonicalID);
			AppendFramed(
				OutManifest,
				LexToString(
					Recipe.ContributorSchemaVersion));
			AppendFramed(
				OutManifest,
				LexToString(
					Recipe.ImplementationRevision));
			AppendFramed(
				OutManifest,
				Recipe.RecipeClassPath);
			if (!Writer.WriteString(CanonicalID)
				|| !Writer.WriteUInt32(
					Recipe.ContributorSchemaVersion)
				|| !Writer.WriteUInt32(
					Recipe.ImplementationRevision)
				|| !Writer.WriteString(
					Recipe.RecipeClassPath))
			{
				OutError = Writer.GetError();
				return false;
			}
		}
		return Writer.Finalize(OutDigest, OutError);
	}
}

struct FSeinCanonicalStateRecipeSnapshot::FData
{
	TArray<FSeinCanonicalStateRecipeDescriptor> Recipes;
	TArray<uint64> ProviderTokens;
	FString CanonicalManifest;
	FGuid ContractDigest;
};

FSeinCanonicalStateRecipeRegistrationHandle::
	~FSeinCanonicalStateRecipeRegistrationHandle()
{
	Reset();
}

FSeinCanonicalStateRecipeRegistrationHandle::
	FSeinCanonicalStateRecipeRegistrationHandle(
		FSeinCanonicalStateRecipeRegistrationHandle&& Other) noexcept
	: Token(Other.Token)
	, bRegistrationSucceeded(Other.bRegistrationSucceeded)
{
	check(IsInGameThread());
	Other.Token = 0;
	Other.bRegistrationSucceeded = false;
}

FSeinCanonicalStateRecipeRegistrationHandle&
FSeinCanonicalStateRecipeRegistrationHandle::operator=(
	FSeinCanonicalStateRecipeRegistrationHandle&& Other) noexcept
{
	check(IsInGameThread());
	if (this != &Other)
	{
		Reset();
		Token = Other.Token;
		bRegistrationSucceeded =
			Other.bRegistrationSucceeded;
		Other.Token = 0;
		Other.bRegistrationSucceeded = false;
	}
	return *this;
}

void FSeinCanonicalStateRecipeRegistrationHandle::Reset()
{
	if (Token == 0)
	{
		return;
	}
	check(IsInGameThread());
	if (IsInvocationActive())
	{
		UE_LOG(
			LogSeinCanonicalStateRecipe,
			Fatal,
			TEXT("A recipe registration handle was destroyed during recipe invocation."));
	}
	const bool bRemoved =
		FSeinCanonicalStateRecipeRegistry::
			UnregisterToken(Token);
	check(bRemoved);
	Token = 0;
	bRegistrationSucceeded = false;
}

int32 FSeinCanonicalStateRecipeSnapshot::GetRecipeCount() const
{
	return Data.IsValid() ? Data->Recipes.Num() : 0;
}

const FString&
FSeinCanonicalStateRecipeSnapshot::GetCanonicalManifest() const
{
	static const FString Empty;
	return Data.IsValid()
		? Data->CanonicalManifest
		: Empty;
}

FGuid FSeinCanonicalStateRecipeSnapshot::GetContractDigest() const
{
	return Data.IsValid()
		? Data->ContractDigest
		: FGuid();
}

TConstArrayView<FSeinCanonicalStateRecipeDescriptor>
FSeinCanonicalStateRecipeSnapshot::GetRecipes() const
{
	return Data.IsValid()
		? TConstArrayView<FSeinCanonicalStateRecipeDescriptor>(
			Data->Recipes)
		: TConstArrayView<
			FSeinCanonicalStateRecipeDescriptor>();
}

FSeinCanonicalStateRecipeRegistrationHandle
FSeinCanonicalStateRecipeRegistry::Register(
	FName OwnerModuleID,
	const FSoftClassPath& RecipeClassPath,
	FString* OutError)
{
	ResetError(OutError);
	if (!IsInGameThread())
	{
		SetError(
			OutError,
			TEXT("Canonical-state recipes may be registered only on the game thread."));
		return {};
	}
	if (IsInvocationActive())
	{
		SetError(
			OutError,
			TEXT("Canonical-state recipe registration is forbidden during recipe invocation."));
		return {};
	}

	const FString CanonicalOwner =
		CanonicalContributorID(OwnerModuleID);
	const FString ClassPath = RecipeClassPath.ToString();
	UClass* IgnoredClass = nullptr;
	const USeinCanonicalStateRecipe* IgnoredRecipe = nullptr;
	FSeinCanonicalStateRecipeDescriptor Descriptor;
	FString Error;
	const bool bDescriptorValid =
		!CanonicalOwner.IsEmpty()
		&& ResolveRecipeClass(
			ClassPath,
			IgnoredClass,
			IgnoredRecipe,
			Descriptor,
			Error);

	const FString CanonicalID =
		CanonicalContributorID(
			Descriptor.StableContributorID);
	FScopeLock Lock(&RegistryMutex());
	const auto MakeFailedClaimLease = []()
	{
		if (NextToken() == 0
			|| NextToken() == MAX_uint64)
		{
			HasUnleasedFailure() = true;
			return FSeinCanonicalStateRecipeRegistrationHandle();
		}
		const uint64 FailedToken = NextToken()++;
		FailedClaimTokens().Add(FailedToken);
		return FSeinCanonicalStateRecipeRegistrationHandle(
			FailedToken, false);
	};

	if (!bDescriptorValid)
	{
		if (CanonicalOwner.IsEmpty())
		{
			Error =
				TEXT("Recipe owner module ID must be a bounded stable ASCII identifier.");
		}
		SetError(OutError, MoveTemp(Error));
		return MakeFailedClaimLease();
	}

	FRegisteredRecipe* Existing =
		Registry().FindByPredicate(
			[&CanonicalID](const FRegisteredRecipe& Candidate)
			{
				return Candidate.CanonicalContributorID
					== CanonicalID;
			});
	if (Existing)
	{
		const bool bExactDuplicate =
			Existing->Claims.Num() > 0
			&& Existing->Claims[0].CanonicalOwnerID
				== CanonicalOwner
			&& DescriptorsMatch(
				Existing->Descriptor, Descriptor);
		if (!bExactDuplicate)
		{
			SetError(
				OutError,
				FString::Printf(
					TEXT("Recipe contributor ID '%s' conflicts with an existing owner or descriptor."),
					*CanonicalID));
			return MakeFailedClaimLease();
		}
		if (Existing->Claims.Num()
			>= MaxReloadClaimsPerRecipe)
		{
			SetError(
				OutError,
				FString::Printf(
					TEXT("Recipe contributor ID '%s' exceeded its reload-claim bound."),
					*CanonicalID));
			return MakeFailedClaimLease();
		}
	}
	else if (Registry().Num() >= MaxRegisteredRecipes)
	{
		SetError(
			OutError,
			TEXT("Canonical-state recipe registry is full."));
		return MakeFailedClaimLease();
	}

	if (NextToken() == 0 || NextToken() == MAX_uint64)
	{
		SetError(
			OutError,
			TEXT("Canonical-state recipe registration token space is exhausted."));
		HasUnleasedFailure() = true;
		return {};
	}
	const uint64 Token = NextToken()++;

	if (!Existing)
	{
		FRegisteredRecipe& Added =
			Registry().AddDefaulted_GetRef();
		Added.CanonicalContributorID =
			CanonicalID;
		Added.Descriptor = Descriptor;
		Existing = &Added;
	}
	Existing->Claims.Add({ Token, CanonicalOwner });
	return FSeinCanonicalStateRecipeRegistrationHandle(
		Token, true);
}

FSeinCanonicalStateRecipeRegistrationHandle
FSeinCanonicalStateRecipeRegistry::RegisterClass(
	FName OwnerModuleID,
	TSubclassOf<USeinCanonicalStateRecipe> RecipeClass,
	FString* OutError)
{
	const UClass* Class = RecipeClass.Get();
	return Register(
		OwnerModuleID,
		FSoftClassPath(
			Class ? Class->GetPathName() : FString()),
		OutError);
}

bool FSeinCanonicalStateRecipeRegistry::Unregister(
	FSeinCanonicalStateRecipeRegistrationHandle& Handle)
{
	if (!Handle.IsValid() || !IsInGameThread())
	{
		return false;
	}
	Handle.Reset();
	return true;
}

bool FSeinCanonicalStateRecipeRegistry::UnregisterToken(
	uint64 Token)
{
	check(IsInGameThread());
	if (IsInvocationActive())
	{
		UE_LOG(
			LogSeinCanonicalStateRecipe,
			Fatal,
			TEXT("Recipe providers may not unregister during recipe invocation."));
	}

	FScopeLock Lock(&RegistryMutex());
	if (FailedClaimTokens().Remove(Token) == 1)
	{
		return true;
	}
	for (int32 RecipeIndex = 0;
		RecipeIndex < Registry().Num();
		++RecipeIndex)
	{
		FRegisteredRecipe& Recipe =
			Registry()[RecipeIndex];
		const int32 Removed =
			Recipe.Claims.RemoveAll(
				[Token](const FRecipeClaim& Claim)
				{
					return Claim.Token == Token;
				});
		if (Removed == 0)
		{
			continue;
		}
		check(Removed == 1);
		if (Recipe.Claims.IsEmpty())
		{
			Registry().RemoveAt(RecipeIndex);
		}
		return true;
	}
	return false;
}

FSeinCanonicalStateRecipeSnapshot
FSeinCanonicalStateRecipeRegistry::Freeze(
	FString* OutError)
{
	ResetError(OutError);
	FSeinCanonicalStateRecipeSnapshot Result;
	if (!IsInGameThread())
	{
		SetError(
			OutError,
			TEXT("Canonical-state recipe registry may be frozen only on the game thread."));
		return Result;
	}
	if (IsInvocationActive())
	{
		SetError(
			OutError,
			TEXT("Canonical-state recipe registry may not freeze during recipe invocation."));
		return Result;
	}

	struct FFrozenCandidate
	{
		FString CanonicalID;
		FSeinCanonicalStateRecipeDescriptor Descriptor;
		uint64 ProviderToken = 0;
	};
	TArray<FFrozenCandidate> Frozen;
	{
		FScopeLock Lock(&RegistryMutex());
		if (HasUnleasedFailure()
			|| !FailedClaimTokens().IsEmpty())
		{
			SetError(
				OutError,
				TEXT("Canonical-state recipe registry contains a failed live module-generation claim."));
			return Result;
		}
		Frozen.Reserve(Registry().Num());
		for (const FRegisteredRecipe& Registered :
			Registry())
		{
			if (Registered.Claims.IsEmpty())
			{
				SetError(
					OutError,
					TEXT("Recipe registry contains an ownerless descriptor."));
				return Result;
			}
			Frozen.Add({
				Registered.CanonicalContributorID,
				Registered.Descriptor,
				Registered.Claims.Last().Token });
		}
	}
	Frozen.Sort(
		[](const FFrozenCandidate& A,
			const FFrozenCandidate& B)
		{
			return A.CanonicalID < B.CanonicalID;
		});

	TSharedRef<FSeinCanonicalStateRecipeSnapshot::FData,
		ESPMode::ThreadSafe> Data =
		MakeShared<
			FSeinCanonicalStateRecipeSnapshot::FData,
			ESPMode::ThreadSafe>();
	Data->Recipes.Reserve(Frozen.Num());
	Data->ProviderTokens.Reserve(Frozen.Num());
	for (const FFrozenCandidate& Candidate : Frozen)
	{
		UClass* IgnoredClass = nullptr;
		const USeinCanonicalStateRecipe* IgnoredRecipe =
			nullptr;
		FSeinCanonicalStateRecipeDescriptor Current;
		FString Error;
		if (!ResolveRecipeClass(
				Candidate.Descriptor.RecipeClassPath,
				IgnoredClass,
				IgnoredRecipe,
				Current,
				Error)
			|| !DescriptorsMatch(
				Candidate.Descriptor, Current))
		{
			if (Error.IsEmpty())
			{
				Error = FString::Printf(
					TEXT("Recipe '%s' changed after registration."),
					*Candidate.CanonicalID);
			}
			SetError(OutError, MoveTemp(Error));
			return Result;
		}
		Data->Recipes.Add(Candidate.Descriptor);
		Data->ProviderTokens.Add(
			Candidate.ProviderToken);
	}

	FString Error;
	if (!BuildRegistryIdentity(
			Data->Recipes,
			Data->CanonicalManifest,
			Data->ContractDigest,
			Error))
	{
		SetError(OutError, MoveTemp(Error));
		return Result;
	}
	Result.Data = Data;
	return Result;
}

bool FSeinCanonicalStateRecipeRegistry::
	DeclareFrozenRecipes(
		const FSeinCanonicalStateRecipeSnapshot& Snapshot,
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeDeclaration>& OutDeclarations,
		FString& OutError)
{
	OutDeclarations.Reset();
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError =
			TEXT("Canonical-state recipes may be declared only on the game thread.");
		return false;
	}
	if (!Snapshot.Data.IsValid()
		|| Snapshot.Data->Recipes.Num()
			!= Snapshot.Data->ProviderTokens.Num())
	{
		OutError =
			TEXT("Canonical-state recipe snapshot is invalid.");
		return false;
	}
	if (IsInvocationActive())
	{
		OutError =
			TEXT("Canonical-state recipe declaration may not be re-entered.");
		return false;
	}

	TArray<FSeinCanonicalStateRecipeDeclaration> Candidate;
	Candidate.Reserve(Snapshot.Data->Recipes.Num());
	TArray<FSeinCanonicalStateRecipeSlotDeclaration> RawSlots;
	FRecipeExecutionReferenceGuard ReferenceGuard(
		MatchSettings, RawSlots, Candidate);
	TSet<FString> GlobalKeys;
	int32 TotalSlots = 0;
	for (int32 RecipeIndex = 0;
		RecipeIndex < Snapshot.Data->Recipes.Num();
		++RecipeIndex)
	{
		ReferenceGuard.ClearRecipe();
		RawSlots.Reset();
		const FSeinCanonicalStateRecipeDescriptor& Descriptor =
			Snapshot.Data->Recipes[RecipeIndex];
		TStrongObjectPtr<UClass> ClassRoot;
		const USeinCanonicalStateRecipe* Recipe = nullptr;
		if (!ResolveFrozenRecipe(
			Descriptor,
			Snapshot.Data->ProviderTokens[RecipeIndex],
			ClassRoot,
			Recipe,
			OutError))
		{
			return false;
		}
		ReferenceGuard.SetRecipe(
			ClassRoot.Get(), Recipe);

		FString RecipeError;
		bool bDeclared = false;
		{
			FRecipeInvocationScope Invocation;
			bDeclared =
				Recipe->DeclareCanonicalStateSlots(
					MatchSettings,
					RawSlots,
					RecipeError);
		}
		if (!bDeclared)
		{
			OutError = FString::Printf(
				TEXT("Recipe '%s' declaration failed: %s"),
				*CanonicalContributorID(
					Descriptor.StableContributorID),
				RecipeError.IsEmpty()
					? TEXT("no reason supplied")
					: *RecipeError);
			return false;
		}

		FSeinCanonicalStateRecipeDeclaration&
			Canonical =
				Candidate.AddDefaulted_GetRef();
		if (!BuildCanonicalDeclaration(
				Descriptor,
				RawSlots,
				GlobalKeys,
				TotalSlots,
				Canonical,
				OutError))
		{
			return false;
		}
	}

	OutDeclarations = MoveTemp(Candidate);
	return true;
}

bool FSeinCanonicalStateRecipeRegistry::
	MaterializeFrozenRecipes(
		const FSeinCanonicalStateRecipeSnapshot& Snapshot,
		const FSeinMatchSettings& MatchSettings,
		TConstArrayView<FSeinCanonicalStateRecipeDeclaration>
			Declarations,
		TArray<FSeinCanonicalStateRecipeMaterialization>&
			OutMaterializations,
		FString& OutError)
{
	OutMaterializations.Reset();
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError =
			TEXT("Canonical-state recipes may materialize only on the game thread.");
		return false;
	}
	if (!Snapshot.Data.IsValid()
		|| Snapshot.Data->Recipes.Num()
			!= Snapshot.Data->ProviderTokens.Num()
		|| Declarations.Num()
			!= Snapshot.Data->Recipes.Num())
	{
		OutError =
			TEXT("Recipe snapshot and declaration set do not match.");
		return false;
	}
	if (IsInvocationActive())
	{
		OutError =
			TEXT("Canonical-state recipe materialization may not be re-entered.");
		return false;
	}

	TMap<FString, const FSeinCanonicalStateRecipeDeclaration*>
		DeclarationsByRecipe;
	for (const FSeinCanonicalStateRecipeDeclaration& Declaration :
		Declarations)
	{
		const FString CanonicalID =
			CanonicalContributorID(
				Declaration.Recipe.StableContributorID);
		if (CanonicalID.IsEmpty()
			|| DeclarationsByRecipe.Contains(CanonicalID))
		{
			OutError =
				TEXT("Recipe declarations contain an invalid or duplicate contributor ID.");
			return false;
		}
		DeclarationsByRecipe.Add(
			CanonicalID, &Declaration);
	}

	TArray<FSeinCanonicalStateRecipeMaterialization>
		Candidate;
	Candidate.Reserve(Snapshot.Data->Recipes.Num());
	FSeinCanonicalStateRecipeDeclaration
		ValidatedDeclaration;
	TArray<FSeinCanonicalStateRecipeInitialValue>
		RawValues;
	FRecipeExecutionReferenceGuard ReferenceGuard(
		MatchSettings,
		Declarations,
		ValidatedDeclaration,
		RawValues,
		Candidate);
	TSet<FString> GlobalKeys;
	int32 TotalSlots = 0;
	for (int32 RecipeIndex = 0;
		RecipeIndex < Snapshot.Data->Recipes.Num();
		++RecipeIndex)
	{
		ReferenceGuard.ClearRecipe();
		ValidatedDeclaration = {};
		RawValues.Reset();
		const FSeinCanonicalStateRecipeDescriptor& Descriptor =
			Snapshot.Data->Recipes[RecipeIndex];
		const FString CanonicalRecipeID =
			CanonicalContributorID(
				Descriptor.StableContributorID);
		const FSeinCanonicalStateRecipeDeclaration* Input =
			DeclarationsByRecipe.FindRef(
				CanonicalRecipeID);
		if (!Input
			|| !DescriptorsMatch(
				Input->Recipe, Descriptor))
		{
			OutError = FString::Printf(
				TEXT("Recipe declaration '%s' does not match its frozen descriptor."),
				*CanonicalRecipeID);
			return false;
		}

		if (!BuildCanonicalDeclaration(
				Descriptor,
				Input->Slots,
				GlobalKeys,
				TotalSlots,
				ValidatedDeclaration,
				OutError)
			|| ValidatedDeclaration.CanonicalSlotDescriptors
				!= Input->CanonicalSlotDescriptors)
		{
			if (OutError.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Recipe declaration '%s' changed after validation."),
					*CanonicalRecipeID);
			}
			return false;
		}

		TStrongObjectPtr<UClass> ClassRoot;
		const USeinCanonicalStateRecipe* Recipe = nullptr;
		if (!ResolveFrozenRecipe(
			Descriptor,
			Snapshot.Data->ProviderTokens[RecipeIndex],
			ClassRoot,
			Recipe,
			OutError))
		{
			return false;
		}
		ReferenceGuard.SetRecipe(
			ClassRoot.Get(), Recipe);

		FString RecipeError;
		bool bMaterialized = false;
		{
			FRecipeInvocationScope Invocation;
			bMaterialized =
				Recipe->MaterializeCanonicalStateValues(
					MatchSettings,
					ValidatedDeclaration.Slots,
					RawValues,
					RecipeError);
		}
		if (!bMaterialized)
		{
			OutError = FString::Printf(
				TEXT("Recipe '%s' materialization failed: %s"),
				*CanonicalRecipeID,
				RecipeError.IsEmpty()
					? TEXT("no reason supplied")
					: *RecipeError);
			return false;
		}
		if (RawValues.Num()
			!= ValidatedDeclaration.Slots.Num())
		{
			OutError = FString::Printf(
				TEXT("Recipe '%s' returned %d values for %d declared slots."),
				*CanonicalRecipeID,
				RawValues.Num(),
				ValidatedDeclaration.Slots.Num());
			return false;
		}

		TMap<FString, const FInstancedStruct*>
			ValuesByKey;
		for (const FSeinCanonicalStateRecipeInitialValue&
			RawValue : RawValues)
		{
			const FString Key =
				FSeinCanonicalStateRegistry::CanonicalKey(
					RawValue.Key);
			if (Key.IsEmpty()
				|| ValuesByKey.Contains(Key))
			{
				OutError = FString::Printf(
					TEXT("Recipe '%s' returned an invalid or duplicate value key."),
					*CanonicalRecipeID);
				return false;
			}
			ValuesByKey.Add(Key, &RawValue.Value);
		}

		FSeinCanonicalStateRecipeMaterialization&
			Materialization =
				Candidate.AddDefaulted_GetRef();
		Materialization.Recipe = Descriptor;
		Materialization.Values.Reserve(
			ValidatedDeclaration.Slots.Num());
		for (const FSeinCanonicalStateRecipeSlotDeclaration&
			Slot : ValidatedDeclaration.Slots)
		{
			const FString Key =
				FSeinCanonicalStateRegistry::CanonicalKey(
					Slot.Definition.Key);
			const FInstancedStruct* const* Value =
				ValuesByKey.Find(Key);
			if (!Value
				|| !ValidateMaterializedValue(
					Slot, **Value, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(
						TEXT("Recipe '%s' omitted declared value '%s'."),
						*CanonicalRecipeID,
						*Key);
				}
				else
				{
					OutError = FString::Printf(
						TEXT("%s value '%s': %s"),
						*CanonicalRecipeID,
						*Key,
						*OutError);
				}
				return false;
			}

			FSeinCanonicalStateRecipeInitialValue&
				CanonicalValue =
					Materialization.Values
						.AddDefaulted_GetRef();
			CanonicalValue.Key =
				Slot.Definition.Key;
			CanonicalValue.Value = **Value;
		}
	}

	OutMaterializations = MoveTemp(Candidate);
	return true;
}

int32 FSeinCanonicalStateRecipeRegistry::
	GetRegisteredRecipeCount()
{
	FScopeLock Lock(&RegistryMutex());
	return Registry().Num();
}

FString FSeinCanonicalStateRecipeRegistry::
	CanonicalContributorID(FName StableContributorID)
{
	if (StableContributorID.IsNone()
		|| StableContributorID.GetNumber() != 0)
	{
		return FString();
	}
	FString Canonical =
		StableContributorID.GetPlainNameString();
	Canonical.ToLowerInline();
	return IsStableASCIIIdentifier(Canonical)
		? Canonical
		: FString();
}
