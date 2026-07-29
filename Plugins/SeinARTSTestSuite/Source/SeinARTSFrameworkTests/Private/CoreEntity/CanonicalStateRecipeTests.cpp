#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Misc/ScopeExit.h"
#include "SeinARTSCoreEntityModule.h"
#include "Serialization/SeinCanonicalStateValueStore.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinCanonicalStateRecipeRegistry.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinCanonicalStateRecipeTestTypes.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#include <initializer_list>

namespace
{
	const FName RecipeTestOwner(
		TEXT("seinframeworktests.recipeowner"));
	const FName AlternateRecipeTestOwner(
		TEXT("seinframeworktests.alternateowner"));
	const FName RecipeTestDomain(
		TEXT("seinframeworktests.recipe"));
	const FGuid RecipeTestContext(
		0x11000000, 0x22000000, 0x33000000, 0x44000000);
	const FGuid RecipeTestPlan(
		0x55000000, 0x66000000, 0x77000000, 0x88000000);
	const FName RecipeTestAuthority(
		TEXT("SeinFrameworkTests.CanonicalStateRecipe"));

	TWeakObjectPtr<USeinWorldSubsystem> GRecipeMutationTarget;
	bool GRecipeMutationAttempted = false;
	TWeakObjectPtr<UScriptStruct> GRecipeProducedRootSchema;
	TWeakObjectPtr<UScriptStruct> GRecipeProducedDynamicSchema;
	const FSeinPlayerID RecipeRejectedPlayer(91);
	const FSeinPlayerID MaterializerAcceptedPlayer(92);

	FSeinCanonicalStateKey MakeRecipeKey(FName Contributor)
	{
		FSeinCanonicalStateKey Key;
		Key.StableDomainId = RecipeTestDomain;
		Key.StableContributorId = Contributor;
		return Key;
	}

	FSeinCanonicalStateRecipeSlotDeclaration MakeRecipeSlot(
		FName Contributor,
		int32 Marker,
		std::initializer_list<int32> OrderedValues = {})
	{
		FSeinCanonicalStateRecipeTestPayload Payload;
		Payload.Marker = Marker;
		for (const int32 Value : OrderedValues)
		{
			Payload.OrderedValues.Add(Value);
		}

		FSeinCanonicalStateRecipeSlotDeclaration Slot;
		Slot.Definition.Key = MakeRecipeKey(Contributor);
		Slot.DefaultValue = FInstancedStruct::Make(Payload);
		return Slot;
	}

	const FSeinCanonicalStateRecipeDescriptor* FindRecipe(
		const FSeinCanonicalStateRecipeSnapshot& Snapshot,
		const TCHAR* CanonicalID)
	{
		for (const FSeinCanonicalStateRecipeDescriptor& Recipe :
			Snapshot.GetRecipes())
		{
			if (FSeinCanonicalStateRecipeRegistry::
				CanonicalContributorID(Recipe.StableContributorID)
					== CanonicalID)
			{
				return &Recipe;
			}
		}
		return nullptr;
	}

	const FSeinCanonicalStateRecipeDeclaration* FindDeclaration(
		TConstArrayView<FSeinCanonicalStateRecipeDeclaration> Declarations,
		const TCHAR* CanonicalID)
	{
		for (const FSeinCanonicalStateRecipeDeclaration& Declaration :
			Declarations)
		{
			if (FSeinCanonicalStateRecipeRegistry::
				CanonicalContributorID(
					Declaration.Recipe.StableContributorID)
					== CanonicalID)
			{
				return &Declaration;
			}
		}
		return nullptr;
	}

	const FSeinCanonicalStateRecipeMaterialization* FindMaterialization(
		TConstArrayView<FSeinCanonicalStateRecipeMaterialization>
			Materializations,
		const TCHAR* CanonicalID)
	{
		for (const FSeinCanonicalStateRecipeMaterialization&
			Materialization : Materializations)
		{
			if (FSeinCanonicalStateRecipeRegistry::
				CanonicalContributorID(
					Materialization.Recipe.StableContributorID)
					== CanonicalID)
			{
				return &Materialization;
			}
		}
		return nullptr;
	}

	bool BuildRecipeValueStore(
		const FSeinCanonicalStateSchemaSnapshot& NativeSchema,
		TConstArrayView<FSeinCanonicalStateRecipeDeclaration> Declarations,
		TConstArrayView<FSeinCanonicalStateRecipeMaterialization>
			Materializations,
		const FString& RecipeManifest,
		FSeinCanonicalStateValueStore& OutStore,
		FString& OutError)
	{
		if (!Materializations.IsEmpty()
			&& Materializations.Num() != Declarations.Num())
		{
			OutError = TEXT("Test materialization count mismatch.");
			return false;
		}

		FSeinCanonicalStateValueStore Candidate;
		for (int32 RecipeIndex = 0;
			RecipeIndex < Declarations.Num();
			++RecipeIndex)
		{
			const FSeinCanonicalStateRecipeDeclaration& Declaration =
				Declarations[RecipeIndex];
			const FSeinCanonicalStateRecipeMaterialization*
				Materialization = Materializations.IsEmpty()
					? nullptr
					: &Materializations[RecipeIndex];
			if (Materialization
				&& Materialization->Values.Num()
					!= Declaration.Slots.Num())
			{
				OutError = TEXT("Test recipe slot count mismatch.");
				return false;
			}

			for (int32 SlotIndex = 0;
				SlotIndex < Declaration.Slots.Num();
				++SlotIndex)
			{
				const FSeinCanonicalStateRecipeSlotDeclaration& Slot =
					Declaration.Slots[SlotIndex];
				const FInstancedStruct& Value = Materialization
					? Materialization->Values[SlotIndex].Value
					: Slot.DefaultValue;
				if (!Candidate.RegisterSlot(
					NativeSchema,
					Slot.Definition,
					Value,
					OutError))
				{
					return false;
				}
			}
		}

		const TArray<FString> RecipeFrames{
			TEXT("SeinARTS.CanonicalState.RecipeBinding\n")
				+ RecipeManifest
		};
		if (!Candidate.Seal(
			NativeSchema, RecipeFrames, OutError))
		{
			return false;
		}
		OutStore = MoveTemp(Candidate);
		return true;
	}

	UScriptStruct* MakeTransientEmptyRecipeStruct(
		const TCHAR* BaseName)
	{
		UScriptStruct* Struct = NewObject<UScriptStruct>(
			GetTransientPackage(),
			MakeUniqueObjectName(
				GetTransientPackage(),
				UScriptStruct::StaticClass(),
				FName(BaseName)),
			RF_Transient);
		if (Struct)
		{
			Struct->Bind();
			Struct->StaticLink(true);
		}
		return Struct;
	}
}

USeinCanonicalStateRecipeAlphaTest::
	USeinCanonicalStateRecipeAlphaTest()
{
	StableContributorID =
		TEXT("seinframeworktests.recipe.alpha");
	ContributorSchemaVersion = 3;
	ImplementationRevision = 7;
}

bool USeinCanonicalStateRecipeAlphaTest::
	DeclareCanonicalStateSlots_Implementation(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const
{
	(void)MatchSettings;
	OutError.Reset();
	OutDeclarations.Reset();
	OutDeclarations.Add(
		MakeRecipeSlot(TEXT("zeta"), 20, { 2, 0 }));
	OutDeclarations.Add(
		MakeRecipeSlot(TEXT("alpha"), 10, { 1, 0 }));
	return true;
}

bool USeinCanonicalStateRecipeAlphaTest::
	MaterializeCanonicalStateValues_Implementation(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const
{
	(void)MatchSettings;
	OutError.Reset();
	OutValues.Reset(Declarations.Num());
	for (int32 Index = Declarations.Num() - 1; Index >= 0; --Index)
	{
		const FSeinCanonicalStateRecipeSlotDeclaration& Declaration =
			Declarations[Index];
		const FString Contributor =
			Declaration.Definition.Key.StableContributorId.ToString();
		FSeinCanonicalStateRecipeTestPayload Payload;
		Payload.Marker = Contributor.Equals(
			TEXT("alpha"), ESearchCase::IgnoreCase)
				? 101
				: 202;

		FSeinCanonicalStateRecipeInitialValue& Value =
			OutValues.AddDefaulted_GetRef();
		Value.Key = Declaration.Definition.Key;
		Value.Value = FInstancedStruct::Make(Payload);
	}
	return true;
}

USeinCanonicalStateRecipeBetaTest::
	USeinCanonicalStateRecipeBetaTest()
{
	StableContributorID =
		TEXT("seinframeworktests.recipe.beta");
	DefaultSlotDeclarations.Add(
		MakeRecipeSlot(TEXT("middle"), 30, { 3, 0 }));
}

USeinCanonicalStateRecipeAlphaConflictTest::
	USeinCanonicalStateRecipeAlphaConflictTest()
{
	StableContributorID =
		TEXT("seinframeworktests.recipe.alpha");
	ImplementationRevision = 99;
	DefaultSlotDeclarations.Add(
		MakeRecipeSlot(TEXT("conflict"), 99));
}

USeinCanonicalStateRecipeDuplicateSlotTest::
	USeinCanonicalStateRecipeDuplicateSlotTest()
{
	StableContributorID =
		TEXT("seinframeworktests.recipe.duplicate");
}

bool USeinCanonicalStateRecipeDuplicateSlotTest::
	DeclareCanonicalStateSlots_Implementation(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const
{
	(void)MatchSettings;
	OutError.Reset();
	OutDeclarations.Reset();
	OutDeclarations.Add(
		MakeRecipeSlot(TEXT("duplicate"), 1));
	OutDeclarations.Add(
		MakeRecipeSlot(TEXT("DUPLICATE"), 2));
	return true;
}

USeinCanonicalStateRecipeWrongValueTypeTest::
	USeinCanonicalStateRecipeWrongValueTypeTest()
{
	StableContributorID =
		TEXT("seinframeworktests.recipe.wrongtype");
	DefaultSlotDeclarations.Add(
		MakeRecipeSlot(TEXT("wrongtype"), 1));
}

bool USeinCanonicalStateRecipeWrongValueTypeTest::
	MaterializeCanonicalStateValues_Implementation(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const
{
	(void)MatchSettings;
	OutError.Reset();
	OutValues.Reset();
	if (Declarations.IsEmpty())
	{
		OutError = TEXT("Expected one declaration.");
		return false;
	}

	FSeinCanonicalStateRecipeInitialValue& Value =
		OutValues.AddDefaulted_GetRef();
	Value.Key = Declarations[0].Definition.Key;
	Value.Value = FInstancedStruct::Make(
		FSeinCanonicalStateRecipeAlternateTestPayload());
	return true;
}

USeinCanonicalStateRecipeMutationProbeTest::
	USeinCanonicalStateRecipeMutationProbeTest()
{
	StableContributorID =
		TEXT("seinframeworktests.recipe.mutationprobe");
	DefaultSlotDeclarations.Add(
		MakeRecipeSlot(TEXT("mutationprobe"), 1));
}

void USeinCanonicalStateRecipeMutationProbeTest::ArmMutationProbe(
	USeinWorldSubsystem* World)
{
	GRecipeMutationTarget = World;
	GRecipeMutationAttempted = false;
}

void USeinCanonicalStateRecipeMutationProbeTest::ResetMutationProbe()
{
	GRecipeMutationTarget.Reset();
	GRecipeMutationAttempted = false;
}

bool USeinCanonicalStateRecipeMutationProbeTest::WasMutationAttempted()
{
	return GRecipeMutationAttempted;
}

bool USeinCanonicalStateRecipeMutationProbeTest::
	MaterializeCanonicalStateValues_Implementation(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const
{
	GRecipeMutationAttempted = true;
	if (USeinWorldSubsystem* World = GRecipeMutationTarget.Get())
	{
		World->RegisterPlayer(
			RecipeRejectedPlayer,
			FSeinFactionID(201),
			/*TeamID=*/9);
	}
	return Super::MaterializeCanonicalStateValues_Implementation(
		MatchSettings, Declarations, OutValues, OutError);
}

USeinCanonicalStateRecipeGCProducerTest::
	USeinCanonicalStateRecipeGCProducerTest()
{
	StableContributorID =
		TEXT("seinframeworktests.recipe.gcproducer");
}

void USeinCanonicalStateRecipeGCProducerTest::ResetGCProbe()
{
	GRecipeProducedRootSchema.Reset();
	GRecipeProducedDynamicSchema.Reset();
}

bool USeinCanonicalStateRecipeGCProducerTest::
	AreProducedSchemasAlive()
{
	return GRecipeProducedRootSchema.IsValid()
		&& GRecipeProducedDynamicSchema.IsValid();
}

bool USeinCanonicalStateRecipeGCProducerTest::
	DeclareCanonicalStateSlots_Implementation(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const
{
	(void)MatchSettings;
	OutDeclarations.Reset();
	OutError.Reset();
	UScriptStruct* RootSchema =
		MakeTransientEmptyRecipeStruct(
			TEXT("SeinRecipeGCRoot"));
	UScriptStruct* DynamicSchema =
		MakeTransientEmptyRecipeStruct(
			TEXT("SeinRecipeGCDynamic"));
	if (!RootSchema || !DynamicSchema)
	{
		OutError =
			TEXT("Could not allocate transient recipe schemas.");
		return false;
	}

	GRecipeProducedRootSchema = RootSchema;
	GRecipeProducedDynamicSchema = DynamicSchema;
	FSeinCanonicalStateRecipeSlotDeclaration& Slot =
		OutDeclarations.AddDefaulted_GetRef();
	Slot.Definition.Key =
		MakeRecipeKey(TEXT("gcproducer"));
	Slot.DefaultValue.InitializeAs(RootSchema);
	FInstancedStruct& DynamicValue =
		Slot.Definition.DynamicPayloadSchemas
			.AddDefaulted_GetRef();
	DynamicValue.InitializeAs(DynamicSchema);
	return Slot.DefaultValue.IsValid()
		&& DynamicValue.IsValid();
}

USeinCanonicalStateRecipeGCTriggerTest::
	USeinCanonicalStateRecipeGCTriggerTest()
{
	StableContributorID =
		TEXT("seinframeworktests.recipe.gctrigger");
}

bool USeinCanonicalStateRecipeGCTriggerTest::
	DeclareCanonicalStateSlots_Implementation(
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinCanonicalStateRecipeSlotDeclaration>& OutDeclarations,
		FString& OutError) const
{
	(void)MatchSettings;
	OutDeclarations.Reset();
	OutError.Reset();
	CollectGarbage(RF_NoFlags);
	return true;
}

bool USeinCanonicalStateRecipeGCTriggerTest::
	MaterializeCanonicalStateValues_Implementation(
		const FSeinMatchSettings& MatchSettings,
		const TArray<FSeinCanonicalStateRecipeSlotDeclaration>& Declarations,
		TArray<FSeinCanonicalStateRecipeInitialValue>& OutValues,
		FString& OutError) const
{
	(void)MatchSettings;
	(void)Declarations;
	OutValues.Reset();
	OutError.Reset();
	CollectGarbage(RF_NoFlags);
	return true;
}

namespace UE::SeinARTSTests
{
	TEST(CanonicalStateRecipeSettingsDriftFailsClosedUntilModuleRestart,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		FSeinARTSCoreEntity& CoreModule =
			FModuleManager::GetModuleChecked<FSeinARTSCoreEntity>(
				TEXT("SeinARTSCoreEntity"));
		USeinARTSCoreSettings* Settings =
			GetMutableDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));

		FString Error;
		ASSERT_THAT(IsTrue(
			CoreModule.ValidateConfiguredCanonicalStateRecipes(Error)));

		const TArray<TSoftClassPtr<USeinCanonicalStateRecipe>>
			PreviousRecipes = Settings->CanonicalStateRecipes;
		ON_SCOPE_EXIT
		{
			Settings->CanonicalStateRecipes = PreviousRecipes;
		};

		Settings->CanonicalStateRecipes.Add(
			USeinCanonicalStateRecipeBetaTest::StaticClass());
		ASSERT_THAT(IsFalse(
			CoreModule.ValidateConfiguredCanonicalStateRecipes(Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("changed after CoreEntity module startup"))));

		Settings->CanonicalStateRecipes = PreviousRecipes;
		ASSERT_THAT(IsTrue(
			CoreModule.ValidateConfiguredCanonicalStateRecipes(Error)));
	}

	TEST(CanonicalStateRecipeSurfaceIsBlueprintAuthorableDataComposition,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		UClass* RecipeClass =
			USeinCanonicalStateRecipe::StaticClass();
		ASSERT_THAT(IsNotNull(RecipeClass));
		ASSERT_THAT(IsTrue(
			RecipeClass->HasAnyClassFlags(CLASS_Abstract)));
		ASSERT_THAT(IsTrue(
			RecipeClass->HasAnyClassFlags(CLASS_Const)));

		UFunction* DeclareFunction =
			RecipeClass->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					USeinCanonicalStateRecipe,
					DeclareCanonicalStateSlots));
		UFunction* MaterializeFunction =
			RecipeClass->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(
					USeinCanonicalStateRecipe,
					MaterializeCanonicalStateValues));
		ASSERT_THAT(IsNotNull(DeclareFunction));
		ASSERT_THAT(IsNotNull(MaterializeFunction));
		if (DeclareFunction && MaterializeFunction)
		{
			const EFunctionFlags RequiredFlags =
				FUNC_Event | FUNC_BlueprintCallable | FUNC_Const;
			ASSERT_THAT(IsTrue(
				DeclareFunction->HasAllFunctionFlags(RequiredFlags)));
			ASSERT_THAT(IsTrue(
				MaterializeFunction->HasAllFunctionFlags(RequiredFlags)));
			ASSERT_THAT(IsFalse(
				DeclareFunction->HasMetaData(TEXT("Latent"))));
			ASSERT_THAT(IsFalse(
				MaterializeFunction->HasMetaData(TEXT("Latent"))));
		}

		const FProperty* DefaultsProperty =
			RecipeClass->FindPropertyByName(
				GET_MEMBER_NAME_CHECKED(
					USeinCanonicalStateRecipe,
					DefaultSlotDeclarations));
		ASSERT_THAT(IsNotNull(DefaultsProperty));
		ASSERT_THAT(IsTrue(
			DefaultsProperty
			&& DefaultsProperty->HasAllPropertyFlags(
				CPF_Edit | CPF_BlueprintVisible
					| CPF_DisableEditOnInstance)));

		const FArrayProperty* SettingsProperty =
			CastField<FArrayProperty>(
				USeinARTSCoreSettings::StaticClass()
					->FindPropertyByName(
						GET_MEMBER_NAME_CHECKED(
							USeinARTSCoreSettings,
							CanonicalStateRecipes)));
		const FSoftClassProperty* RecipeClassProperty =
			SettingsProperty
				? CastField<FSoftClassProperty>(
					SettingsProperty->Inner)
				: nullptr;
		ASSERT_THAT(IsNotNull(SettingsProperty));
		ASSERT_THAT(IsNotNull(RecipeClassProperty));
	}

	TEST(CanonicalStateRecipeOrderingIgnoresRegistrationAndAuthoredOrder,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		const int32 BaselineCount =
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount();
		FString Error;

		FSeinCanonicalStateRecipeRegistrationHandle Beta =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeBetaTest::StaticClass(),
				&Error);
		FSeinCanonicalStateRecipeRegistrationHandle Alpha =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeAlphaTest::StaticClass(),
				&Error);
		ASSERT_THAT(IsTrue(Beta.IsValid()));
		ASSERT_THAT(IsTrue(Alpha.IsValid()));

		const FSeinCanonicalStateRecipeSnapshot ReverseRegistration =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(ReverseRegistration.IsValid()));
		ASSERT_THAT(AreEqual(
			BaselineCount + 2,
			ReverseRegistration.GetRecipeCount()));
		const FSeinCanonicalStateRecipeDescriptor* FrozenAlpha =
			FindRecipe(
				ReverseRegistration,
				TEXT("seinframeworktests.recipe.alpha"));
		const FSeinCanonicalStateRecipeDescriptor* FrozenBeta =
			FindRecipe(
				ReverseRegistration,
				TEXT("seinframeworktests.recipe.beta"));
		ASSERT_THAT(IsNotNull(FrozenAlpha));
		ASSERT_THAT(IsNotNull(FrozenBeta));
		ASSERT_THAT(IsTrue(
			FrozenAlpha && FrozenBeta && FrozenAlpha < FrozenBeta));

		TArray<FSeinCanonicalStateRecipeDeclaration> Declarations;
		const FSeinMatchSettings Settings;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRecipeRegistry::DeclareFrozenRecipes(
				ReverseRegistration,
				Settings,
				Declarations,
				Error)));
		const FSeinCanonicalStateRecipeDeclaration* AlphaDeclaration =
			FindDeclaration(
				Declarations,
				TEXT("seinframeworktests.recipe.alpha"));
		ASSERT_THAT(IsNotNull(AlphaDeclaration));
		ASSERT_THAT(AreEqual(
			2,
			AlphaDeclaration
				? AlphaDeclaration->Slots.Num()
				: 0));
		if (AlphaDeclaration
			&& AlphaDeclaration->Slots.Num() == 2)
		{
			ASSERT_THAT(AreEqual(
				FString(TEXT("alpha")),
				AlphaDeclaration->Slots[0]
					.Definition.Key.StableContributorId
					.ToString()));
			ASSERT_THAT(AreEqual(
				FString(TEXT("zeta")),
				AlphaDeclaration->Slots[1]
					.Definition.Key.StableContributorId
					.ToString()));
		}

		TArray<FSeinCanonicalStateRecipeMaterialization>
			Materializations;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRecipeRegistry::
				MaterializeFrozenRecipes(
					ReverseRegistration,
					Settings,
					Declarations,
					Materializations,
					Error)));
		const FSeinCanonicalStateRecipeMaterialization*
			AlphaMaterialization = FindMaterialization(
				Materializations,
				TEXT("seinframeworktests.recipe.alpha"));
		ASSERT_THAT(IsNotNull(AlphaMaterialization));
		ASSERT_THAT(AreEqual(
			2,
			AlphaMaterialization
				? AlphaMaterialization->Values.Num()
				: 0));
		if (AlphaMaterialization
			&& AlphaMaterialization->Values.Num() == 2)
		{
			ASSERT_THAT(AreEqual(
				101,
				AlphaMaterialization->Values[0].Value
					.Get<FSeinCanonicalStateRecipeTestPayload>()
					.Marker));
			ASSERT_THAT(AreEqual(
				202,
				AlphaMaterialization->Values[1].Value
					.Get<FSeinCanonicalStateRecipeTestPayload>()
					.Marker));
		}

		const FString ReverseManifest =
			ReverseRegistration.GetCanonicalManifest();
		const FGuid ReverseDigest =
			ReverseRegistration.GetContractDigest();
		Alpha.Reset();
		Beta.Reset();

		Alpha =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeAlphaTest::StaticClass(),
				&Error);
		Beta =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeBetaTest::StaticClass(),
				&Error);
		const FSeinCanonicalStateRecipeSnapshot ForwardRegistration =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(ForwardRegistration.IsValid()));
		ASSERT_THAT(AreEqual(
			ReverseManifest,
			ForwardRegistration.GetCanonicalManifest()));
		ASSERT_THAT(IsTrue(
			ReverseDigest
				== ForwardRegistration.GetContractDigest()));
	}

	TEST(CanonicalStateRecipeReloadOverlapNeverRetargetsFrozenGeneration,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		const int32 BaselineCount =
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount();
		FString Error;
		FSeinCanonicalStateRecipeRegistrationHandle Previous =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeAlphaTest::StaticClass(),
				&Error);
		FSeinCanonicalStateRecipeRegistrationHandle Replacement =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeAlphaTest::StaticClass(),
				&Error);
		ASSERT_THAT(IsTrue(Previous.IsValid()));
		ASSERT_THAT(IsTrue(Replacement.IsValid()));
		ASSERT_THAT(AreEqual(
			BaselineCount + 1,
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount()));

		const FSeinCanonicalStateRecipeSnapshot ReplacementSnapshot =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(ReplacementSnapshot.IsValid()));
		Replacement.Reset();

		TArray<FSeinCanonicalStateRecipeDeclaration> Declarations;
		ASSERT_THAT(IsFalse(
			FSeinCanonicalStateRecipeRegistry::DeclareFrozenRecipes(
				ReplacementSnapshot,
				FSeinMatchSettings(),
				Declarations,
				Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("unregistered or replaced"))));
		ASSERT_THAT(IsTrue(Declarations.IsEmpty()));

		const FSeinCanonicalStateRecipeSnapshot PreviousSnapshot =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(PreviousSnapshot.IsValid()));
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRecipeRegistry::DeclareFrozenRecipes(
				PreviousSnapshot,
				FSeinMatchSettings(),
				Declarations,
				Error)));
		Previous.Reset();
		ASSERT_THAT(AreEqual(
			BaselineCount,
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount()));
	}

	TEST(CanonicalStateRecipeRegistrationRejectsIdentityCollisions,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		const int32 BaselineCount =
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount();
		FString Error;
		FSeinCanonicalStateRecipeRegistrationHandle Alpha =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeAlphaTest::StaticClass(),
				&Error);
		ASSERT_THAT(IsTrue(Alpha.IsValid()));

		FSeinCanonicalStateRecipeRegistrationHandle Conflict =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeAlphaConflictTest::
					StaticClass(),
				&Error);
		ASSERT_THAT(IsFalse(Conflict.IsValid()));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("conflicts"))));

		FSeinCanonicalStateRecipeRegistrationHandle WrongOwner =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				AlternateRecipeTestOwner,
				USeinCanonicalStateRecipeAlphaTest::StaticClass(),
				&Error);
		ASSERT_THAT(IsFalse(WrongOwner.IsValid()));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("conflicts"))));
		ASSERT_THAT(AreEqual(
			BaselineCount + 1,
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount()));

		const FSeinCanonicalStateRecipeSnapshot BothClaimsLive =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsFalse(BothClaimsLive.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("failed live module-generation claim"))));

		Conflict.Reset();
		const FSeinCanonicalStateRecipeSnapshot OneClaimLive =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsFalse(OneClaimLive.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("failed live module-generation claim"))));

		WrongOwner.Reset();
		const FSeinCanonicalStateRecipeSnapshot Recovered =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(Recovered.IsValid()));
		ASSERT_THAT(AreEqual(
			BaselineCount + 1,
			Recovered.GetRecipeCount()));
	}

	TEST(CanonicalStateRecipeInvalidClaimPoisonsFreezeUntilReleased,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		const int32 BaselineCount =
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount();
		FString Error;
		{
			FSeinCanonicalStateRecipeRegistrationHandle Invalid =
				FSeinCanonicalStateRecipeRegistry::Register(
					RecipeTestOwner,
					FSoftClassPath(),
					&Error);
			ASSERT_THAT(IsFalse(Invalid.IsValid()));
			ASSERT_THAT(IsFalse(Error.IsEmpty()));
			ASSERT_THAT(AreEqual(
				BaselineCount,
				FSeinCanonicalStateRecipeRegistry::
					GetRegisteredRecipeCount()));

			const FSeinCanonicalStateRecipeSnapshot Poisoned =
				FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
			ASSERT_THAT(IsFalse(Poisoned.IsValid()));
			ASSERT_THAT(IsTrue(
				Error.Contains(
					TEXT("failed live module-generation claim"))));
		}
		const FSeinCanonicalStateRecipeSnapshot Recovered =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(Recovered.IsValid()));
		ASSERT_THAT(AreEqual(
			BaselineCount,
			Recovered.GetRecipeCount()));
	}

	TEST(CanonicalStateRecipeReloadClaimBoundFailsClosed,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		const int32 BaselineCount =
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount();
		FString Error;
		TArray<FSeinCanonicalStateRecipeRegistrationHandle> Claims;
		Claims.Reserve(
			FSeinCanonicalStateRecipeRegistry::
				MaxReloadClaimsPerRecipe);
		for (int32 ClaimIndex = 0;
			ClaimIndex
				< FSeinCanonicalStateRecipeRegistry::
					MaxReloadClaimsPerRecipe;
			++ClaimIndex)
		{
			FSeinCanonicalStateRecipeRegistrationHandle Claim =
				FSeinCanonicalStateRecipeRegistry::RegisterClass(
					RecipeTestOwner,
					USeinCanonicalStateRecipeAlphaTest::
						StaticClass(),
					&Error);
			ASSERT_THAT(IsTrue(Claim.IsValid()));
			Claims.Add(MoveTemp(Claim));
		}
		ASSERT_THAT(AreEqual(
			BaselineCount + 1,
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount()));

		FSeinCanonicalStateRecipeRegistrationHandle Overflow =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeAlphaTest::StaticClass(),
				&Error);
		ASSERT_THAT(IsFalse(Overflow.IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("reload-claim bound"))));
		ASSERT_THAT(IsFalse(
			FSeinCanonicalStateRecipeRegistry::Freeze(
				&Error).IsValid()));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("failed live module-generation claim"))));

		Overflow.Reset();
		const FSeinCanonicalStateRecipeSnapshot Recovered =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(Recovered.IsValid()));
		ASSERT_THAT(AreEqual(
			BaselineCount + 1,
			Recovered.GetRecipeCount()));
		Claims.Reset();
		ASSERT_THAT(AreEqual(
			BaselineCount,
			FSeinCanonicalStateRecipeRegistry::
				GetRegisteredRecipeCount()));
	}

	TEST(CanonicalStateRecipeValidationIsTransactional,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		FString Error;
		FSeinCanonicalStateRecipeRegistrationHandle Duplicate =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeDuplicateSlotTest::
					StaticClass(),
				&Error);
		ASSERT_THAT(IsTrue(Duplicate.IsValid()));
		const FSeinCanonicalStateRecipeSnapshot DuplicateSnapshot =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(DuplicateSnapshot.IsValid()));

		TArray<FSeinCanonicalStateRecipeDeclaration>
			RejectedDeclarations;
		RejectedDeclarations.AddDefaulted();
		ASSERT_THAT(IsFalse(
			FSeinCanonicalStateRecipeRegistry::DeclareFrozenRecipes(
				DuplicateSnapshot,
				FSeinMatchSettings(),
				RejectedDeclarations,
				Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("declared more than once"))));
		ASSERT_THAT(IsTrue(RejectedDeclarations.IsEmpty()));
		Duplicate.Reset();

		FSeinCanonicalStateRecipeRegistrationHandle WrongType =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeWrongValueTypeTest::
					StaticClass(),
				&Error);
		ASSERT_THAT(IsTrue(WrongType.IsValid()));
		const FSeinCanonicalStateRecipeSnapshot WrongTypeSnapshot =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		TArray<FSeinCanonicalStateRecipeDeclaration>
			ValidDeclarations;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRecipeRegistry::DeclareFrozenRecipes(
				WrongTypeSnapshot,
				FSeinMatchSettings(),
				ValidDeclarations,
				Error)));

		TArray<FSeinCanonicalStateRecipeMaterialization>
			RejectedMaterializations;
		RejectedMaterializations.AddDefaulted();
		ASSERT_THAT(IsFalse(
			FSeinCanonicalStateRecipeRegistry::
				MaterializeFrozenRecipes(
					WrongTypeSnapshot,
					FSeinMatchSettings(),
					ValidDeclarations,
					RejectedMaterializations,
					Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("root payload type"))));
		ASSERT_THAT(IsTrue(
			RejectedMaterializations.IsEmpty()));
	}

	TEST(CanonicalStateRecipeRestoreUsesOnlyLocalDeclaredSchema,
		"SeinARTS.Determinism.CoreEntity.CanonicalState.Recipe")
	{
		FString Error;
		FSeinCanonicalStateRecipeRegistrationHandle Alpha =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeAlphaTest::StaticClass(),
				&Error);
		ASSERT_THAT(IsTrue(Alpha.IsValid()));
		const FSeinCanonicalStateRecipeSnapshot Snapshot =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(Snapshot.IsValid()));

		const FSeinMatchSettings Settings;
		TArray<FSeinCanonicalStateRecipeDeclaration> Declarations;
		TArray<FSeinCanonicalStateRecipeMaterialization>
			Materializations;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRecipeRegistry::DeclareFrozenRecipes(
				Snapshot, Settings, Declarations, Error)));
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRecipeRegistry::
				MaterializeFrozenRecipes(
					Snapshot,
					Settings,
					Declarations,
					Materializations,
					Error)));

		const FSeinCanonicalStateSchemaSnapshot NativeSchema =
			FSeinCanonicalStateRegistry::CaptureSchemaSnapshot(
				&Error);
		ASSERT_THAT(IsTrue(NativeSchema.IsValid()));
		FSeinCanonicalStateValueStore Source;
		FSeinCanonicalStateValueStore ExpectedSchema;
		ASSERT_THAT(IsTrue(BuildRecipeValueStore(
			NativeSchema,
			Declarations,
			Materializations,
			Snapshot.GetCanonicalManifest(),
			Source,
			Error)));
		ASSERT_THAT(IsTrue(BuildRecipeValueStore(
			NativeSchema,
			Declarations,
			{},
			Snapshot.GetCanonicalManifest(),
			ExpectedSchema,
			Error)));
		ASSERT_THAT(IsTrue(
			Source.GetContractDigest()
				== ExpectedSchema.GetContractDigest()));

		TArray<FSeinCanonicalStateValueRecord> Records;
		ASSERT_THAT(IsTrue(Source.CaptureRecords(Records, Error)));
		FSeinCanonicalStateValueStore Restored;
		ASSERT_THAT(IsTrue(Restored.TryRestoreRecords(
			ExpectedSchema,
			Records,
			Source.GetContractDigest(),
			Error)));

		FInstancedStruct AlphaValue;
		ASSERT_THAT(IsTrue(Restored.GetValue(
			MakeRecipeKey(TEXT("alpha")),
			AlphaValue)));
		ASSERT_THAT(AreEqual(
			101,
			AlphaValue
				.Get<FSeinCanonicalStateRecipeTestPayload>()
				.Marker));

		TArray<FSeinCanonicalStateValueRecord> HostileRecords =
			Records;
		ASSERT_THAT(IsFalse(HostileRecords.IsEmpty()));
		HostileRecords[0].PayloadStructPath =
			FSeinCanonicalStateRecipeAlternateTestPayload::
				StaticStruct()->GetPathName();
		HostileRecords[0].Limits.MaxEncodedBytes += 1;
		ASSERT_THAT(IsFalse(Restored.TryRestoreRecords(
			ExpectedSchema,
			HostileRecords,
			Source.GetContractDigest(),
			Error)));
		ASSERT_THAT(IsTrue(
			Error.Contains(TEXT("local declaration"))));

		FInstancedStruct UnchangedValue;
		ASSERT_THAT(IsTrue(Restored.GetValue(
			MakeRecipeKey(TEXT("alpha")),
			UnchangedValue)));
		ASSERT_THAT(AreEqual(
			101,
			UnchangedValue
				.Get<FSeinCanonicalStateRecipeTestPayload>()
				.Marker));
	}

	TEST(CanonicalStateRecipeTransactionsRootAccumulatedReflectedSchemas,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		USeinCanonicalStateRecipeGCProducerTest::ResetGCProbe();
		ON_SCOPE_EXIT
		{
			USeinCanonicalStateRecipeGCProducerTest::
				ResetGCProbe();
		};

		FString Error;
		FSeinCanonicalStateRecipeRegistrationHandle Trigger =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeGCTriggerTest::
					StaticClass(),
				&Error);
		FSeinCanonicalStateRecipeRegistrationHandle Producer =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeGCProducerTest::
					StaticClass(),
				&Error);
		ASSERT_THAT(IsTrue(Trigger.IsValid()));
		ASSERT_THAT(IsTrue(Producer.IsValid()));

		const FSeinCanonicalStateRecipeSnapshot Snapshot =
			FSeinCanonicalStateRecipeRegistry::Freeze(&Error);
		ASSERT_THAT(IsTrue(Snapshot.IsValid()));
		TArray<FSeinCanonicalStateRecipeDeclaration> Declarations;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRecipeRegistry::DeclareFrozenRecipes(
				Snapshot,
				FSeinMatchSettings(),
				Declarations,
				Error)));
		ASSERT_THAT(IsTrue(
			USeinCanonicalStateRecipeGCProducerTest::
				AreProducedSchemasAlive()));
		const FSeinCanonicalStateRecipeDeclaration*
			ProducerDeclaration = FindDeclaration(
				Declarations,
				TEXT("seinframeworktests.recipe.gcproducer"));
		ASSERT_THAT(IsNotNull(ProducerDeclaration));
		ASSERT_THAT(AreEqual(
			1,
			ProducerDeclaration
				? ProducerDeclaration->Slots.Num()
				: 0));

		TArray<FSeinCanonicalStateRecipeMaterialization>
			Materializations;
		ASSERT_THAT(IsTrue(
			FSeinCanonicalStateRecipeRegistry::
				MaterializeFrozenRecipes(
					Snapshot,
					FSeinMatchSettings(),
					Declarations,
					Materializations,
					Error)));
		ASSERT_THAT(IsTrue(
			USeinCanonicalStateRecipeGCProducerTest::
				AreProducedSchemasAlive()));
		const FSeinCanonicalStateRecipeMaterialization*
			ProducerMaterialization = FindMaterialization(
				Materializations,
				TEXT("seinframeworktests.recipe.gcproducer"));
		ASSERT_THAT(IsNotNull(ProducerMaterialization));
		ASSERT_THAT(AreEqual(
			1,
			ProducerMaterialization
				? ProducerMaterialization->Values.Num()
				: 0));
	}

	TEST(CanonicalStateRecipeCannotBorrowMaterializerMutationCapability,
		"SeinARTS.Unit.CoreEntity.CanonicalState.Recipe")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		FSeinCanonicalStateRecipeRegistrationHandle Probe =
			FSeinCanonicalStateRecipeRegistry::RegisterClass(
				RecipeTestOwner,
				USeinCanonicalStateRecipeMutationProbeTest::
					StaticClass(),
				&Error);
		ASSERT_THAT(IsTrue(Probe.IsValid()));

		USeinCanonicalStateRecipeMutationProbeTest::
			ArmMutationProbe(World);
		ON_SCOPE_EXIT
		{
			USeinCanonicalStateRecipeMutationProbeTest::
				ResetMutationProbe();
		};

		FSeinMatchBootstrapAuthorityHandle Authority;
		ASSERT_THAT(IsTrue(
			World->ClaimMatchBootstrapAuthority(
				RecipeTestAuthority,
				World,
				Authority,
				Error)));
		ASSERT_THAT(IsTrue(
			World->SeedSimRandom(Authority, 12345, Error)));

		bool bMaterializerHadMutationAuthority = false;
		const FSeinMatchBootstrapMaterializer PreviousMaterializer =
			World->MatchBootstrapMaterializer;
		ON_SCOPE_EXIT
		{
			World->MatchBootstrapMaterializer =
				PreviousMaterializer;
		};
		World->MatchBootstrapMaterializer.BindLambda(
			[World, &bMaterializerHadMutationAuthority](
				const FSeinMatchSettings& MatchSettings,
				const FGuid& Context,
				FSeinMatchBootstrapReceipt& OutReceipt,
				FString& OutError)
			{
				if (Context != RecipeTestContext
					|| World->GetPlayerState(
						RecipeRejectedPlayer))
				{
					OutError =
						TEXT("Recipe leaked mutation capability.");
					return false;
				}

				World->RegisterPlayer(
					MaterializerAcceptedPlayer,
					FSeinFactionID(202),
					/*TeamID=*/9);
				bMaterializerHadMutationAuthority =
					World->GetPlayerState(
						MaterializerAcceptedPlayer)
					!= nullptr;
				World->StartMatch(MatchSettings);
				return bMaterializerHadMutationAuthority
					&& World->GetMatchState()
						== ESeinMatchState::Starting
					&& World->SealLocalMatchBootstrap(
						RecipeTestPlan,
						OutReceipt,
						OutError);
			});

		TestRunner->AddExpectedError(
			TEXT("RegisterPlayer rejected outside bootstrap Applying"),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		FSeinMatchBootstrapReceipt Receipt;
		ASSERT_THAT(IsTrue(
			World->EnsureMatchBootstrapLocallyReady(
				Authority,
				FSeinMatchSettings(),
				RecipeTestContext,
				Receipt,
				Error)));
		ASSERT_THAT(IsTrue(
			USeinCanonicalStateRecipeMutationProbeTest::
				WasMutationAttempted()));
		ASSERT_THAT(IsNull(
			World->GetPlayerState(RecipeRejectedPlayer)));
		ASSERT_THAT(IsTrue(bMaterializerHadMutationAuthority));
		ASSERT_THAT(IsNotNull(
			World->GetPlayerState(
				MaterializerAcceptedPlayer)));
		ASSERT_THAT(IsTrue(Receipt.IsValid()));
	}
}
