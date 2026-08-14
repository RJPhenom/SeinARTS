/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinBlueprintDeterminismValidator.cpp
 * @author       RJ Macklem
 * @created      24 Jun 2026
 * @latest       14 Aug 2026
 * @brief        Implements shared deterministic state and call validation for sim Blueprints.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Validators/SeinBlueprintDeterminismValidator.h"

#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MacroInstance.h"
#include "EdGraph/EdGraph.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/DataValidation.h"
#include "Util/SeinDeterminismRules.h"  // SeinDeterminism::IsPinTypeDeterministic (member-var check)
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "SeinBlueprintDeterminismValidator"

namespace
{
	const FName SeinDeterministicMeta(TEXT("SeinDeterministic"));
	const FName SeinPresentationOnlyMeta(TEXT("SeinPresentationOnly"));

	/** True if an FProperty's type is deterministic-safe: bool, integer types, byte/enum, FName,
	 *  or a SeinDeterministic-marked struct. Rejects float/double, object/soft/class refs,
	 *  string/text, etc. (Reflection-level mirror of SeinDeterminism::IsPinTypeDeterministic.) */
	bool IsPropertyTypeDeterministic(const FProperty* Prop)
	{
		if (!Prop) return false;
		if (Prop->IsA<FBoolProperty>()
		 || Prop->IsA<FByteProperty>()
		 || Prop->IsA<FIntProperty>()
		 || Prop->IsA<FInt64Property>()
		 || Prop->IsA<FNameProperty>()
		 || Prop->IsA<FEnumProperty>())
		{
			return true;
		}
		if (const FStructProperty* SP = CastField<FStructProperty>(Prop))
		{
			return SP->Struct && SP->Struct->HasMetaData(SeinDeterministicMeta);
		}
		return false;
	}

	/** True if every parameter and return value uses the deterministic value set. This is only a
	 *  secondary gate for the explicitly audited Kismet math owner; a safe-looking signature alone
	 *  never certifies behavior. */
	bool IsFunctionSignatureDeterministic(const UFunction* Func)
	{
		for (TFieldIterator<FProperty> It(Func); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (!IsPropertyTypeDeterministic(*It)) return false;
		}
		return true;
	}

	/** Kismet math calls whose signatures look deterministic but whose behavior uses unseeded RNG. */
	bool IsKnownNonDeterministicCall(const UFunction* Func)
	{
		if (!Func) return false;
		static const TSet<FName> Denylist = {
			FName(TEXT("RandomInteger")),   FName(TEXT("RandomIntegerInRange")),
			FName(TEXT("RandomInteger64")), FName(TEXT("RandomInteger64InRange")),
			FName(TEXT("RandomBool")),      FName(TEXT("RandRange")),
			FName(TEXT("RandomFloat")),     FName(TEXT("RandomFloatInRange")),
			FName(TEXT("FRand")),           FName(TEXT("Rand"))
		};
		return Denylist.Contains(Func->GetFName());
	}

	/** Verdict for one call. Presentation-only metadata always wins. Explicit
	 *  deterministic function/class ownership certifies the call. The only
	 *  engine fallback is the audited deterministic-signature subset of
	 *  UKismetMathLibrary, excluding its unseeded random calls. Everything else
	 *  fails closed. Unresolved targets are left to the Blueprint compiler. */
	bool IsCallNonDeterministic(const UFunction* Func)
	{
		if (!Func) return false;
		if (Func->HasMetaData(SeinPresentationOnlyMeta)) return true;
		const UClass* Owner = Func->GetOwnerClass();
		if (Owner && Owner->HasMetaData(SeinPresentationOnlyMeta)) return true;
		if (Func->HasMetaData(SeinDeterministicMeta)) return false;
		if (Owner && Owner->HasMetaData(SeinDeterministicMeta)) return false;
		if (Owner == UKismetMathLibrary::StaticClass())
		{
			return IsKnownNonDeterministicCall(Func)
				|| !IsFunctionSignatureDeterministic(Func);
		}
		return true;
	}

	/** Recursively gather call nodes reachable through macro instances — a macro's graph lives in
	 *  another asset, so the direct BP walk misses non-deterministic calls hidden inside it. Bounded
	 *  against cycles by `Visited`. */
	void CollectMacroCallNodes(UEdGraph* MacroGraph, TSet<UEdGraph*>& Visited, TArray<UK2Node_CallFunction*>& OutCalls)
	{
		if (!MacroGraph || Visited.Contains(MacroGraph)) return;
		Visited.Add(MacroGraph);
		for (UEdGraphNode* Node : MacroGraph->Nodes)
		{
			if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				OutCalls.Add(Call);
			}
			else if (UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
			{
				CollectMacroCallNodes(Macro->GetMacroGraph(), Visited, OutCalls);
			}
		}
	}
}

USeinBlueprintDeterminismValidator::USeinBlueprintDeterminismValidator()
{
	bIsEnabled = true;
}

FText USeinBlueprintDeterminismValidator::GetAssetKindLabel() const
{
	return LOCTEXT("DefaultKind", "Sim Blueprint");
}

FText USeinBlueprintDeterminismValidator::GetToolkitHintText() const
{
	return LOCTEXT("DefaultHint", "deterministic toolkit");
}

bool USeinBlueprintDeterminismValidator::CanValidateAsset_Implementation(const FAssetData& /*InAssetData*/, UObject* InObject, FDataValidationContext& /*InContext*/) const
{
	return IsTargetBlueprint(Cast<UBlueprint>(InObject));
}

EDataValidationResult USeinBlueprintDeterminismValidator::ValidateLoadedAsset_Implementation(const FAssetData& /*InAssetData*/, UObject* InAsset, FDataValidationContext& /*Context*/)
{
	UBlueprint* BP = Cast<UBlueprint>(InAsset);
	if (!BP)
	{
		return EDataValidationResult::NotValidated;
	}

	// Gather every function-call node — directly in the BP, and recursively inside any macro
	// instances (whose graphs live in other assets, so the direct walk would miss them).
	TArray<UK2Node_CallFunction*> CallNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CallFunction>(BP, CallNodes);
	{
		TArray<UK2Node_MacroInstance*> MacroNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_MacroInstance>(BP, MacroNodes);
		TSet<UEdGraph*> Visited;
		for (UK2Node_MacroInstance* Macro : MacroNodes)
		{
			if (Macro) { CollectMacroCallNodes(Macro->GetMacroGraph(), Visited, CallNodes); }
		}
	}

	const bool bAsError    = ShouldEscalateToError();
	const FText KindLabel  = GetAssetKindLabel();
	const FText HintText   = GetToolkitHintText();
	bool bAnyFlagged       = false;

	for (UK2Node_CallFunction* Node : CallNodes)
	{
		if (!Node) continue;
		const UFunction* Func = Node->GetTargetFunction();
		if (!IsCallNonDeterministic(Func)) continue;

		bAnyFlagged = true;
		const FString FuncName  = Func ? Func->GetName() : TEXT("<unresolved>");
		const UClass* Owner     = Func ? Func->GetOwnerClass() : nullptr;
		const FString OwnerName = Owner ? Owner->GetName() : TEXT("<unknown>");
		const FText Msg = FText::Format(
			LOCTEXT("NonDeterministicCall",
				"{0} calls non-deterministic '{1}::{2}' — this can desync lockstep. Use the SeinARTS "
				"fixed-point math / {3} nodes, or confirm the call is deterministic."),
			KindLabel, FText::FromString(OwnerName), FText::FromString(FuncName), HintText);

		if (bAsError) { AssetFails(InAsset, Msg); }
		else          { AssetWarning(InAsset, Msg); }
	}

	// Member variables: the call walk catches non-deterministic CALLS, not non-deterministic STATE.
	// A movement mode persists per-unit (and a formation must stay stateless), so a non-deterministic-
	// typed member variable is loose authoritative state that can desync lockstep. Tuning vars are
	// deterministic-typed (they pass) + hydrated; floats / vector-floats / object refs kept as scratch
	// are what's flagged. Render-only values belong outside the sim (e.g. Set Render Value), not here.
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (SeinDeterminism::IsPinTypeDeterministic(Var.VarType)) continue;

		bAnyFlagged = true;
		const FText Msg = FText::Format(
			LOCTEXT("NonDeterministicVar",
				"{0} variable '{1}' is a non-deterministic type — a {0} runs in the deterministic sim and "
				"persists per-unit, so non-deterministic member state can desync lockstep. Use fixed-point "
				"types (FFixedPoint / FFixedVector), or keep render-only values out of the simulation."),
			KindLabel, FText::FromName(Var.VarName));

		if (bAsError) { AssetFails(InAsset, Msg); }
		else          { AssetWarning(InAsset, Msg); }
	}

	if (bAsError && bAnyFlagged)
	{
		return EDataValidationResult::Invalid;
	}

	// Warnings (or a clean asset) never block. AssetPasses marks it as checked.
	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
