/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    K2Node_SeinSetComponent.cpp
 */

#include "Graph/K2Node_SeinSetComponent.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "Styling/SlateIconFinder.h"
#include "UObject/UObjectIterator.h"
#include "StructUtils/UserDefinedStruct.h"

#include "Components/SeinComponent.h"
#include "Core/SeinEntityHandle.h"
#include "Lib/SeinComponentBPFL.h"

// Inlined SeinDeterministic meta key — same name the factory uses. K2 nodes
// can't dep on SeinARTSEditor (Editor module type) because they live in an
// UncookedOnly module, so we duplicate the constant.
namespace { static const FName GSeinDeterministicMetaKey(TEXT("SeinDeterministic")); }

#define LOCTEXT_NAMESPACE "K2Node_SeinSetComponent"

namespace SeinK2SetCompLocal
{
	static const FName PN_WorldContext(TEXT("WorldContextObject"));
	static const FName PN_EntityHandle(TEXT("EntityHandle"));
	static const FName PN_InStruct(TEXT("InStruct"));
	static const FName PN_Success(TEXT("ReturnValue"));

	// Mirror the Get-node helpers so the menu actions stay in sync. Inlined
	// instead of shared to keep the two K2 nodes loosely coupled — each one
	// can evolve its filtering policy independently.
	static void GatherCandidateStructs(TArray<UScriptStruct*>& Out)
	{
		const UScriptStruct* Base = FSeinComponent::StaticStruct();
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* S = *It;
			if (!S || S == Base) continue;
			if (S->IsChildOf(Base)) { Out.Add(S); continue; }
			if (S->IsA<UUserDefinedStruct>() && S->HasMetaData(GSeinDeterministicMetaKey))
			{
				Out.Add(S);
			}
		}
		Out.Sort([](const UScriptStruct& A, const UScriptStruct& B)
		{
			return A.GetFName().LexicalLess(B.GetFName());
		});
	}

	static FString PrettyName(const UScriptStruct* S)
	{
		if (!S) return FString();
		FString N = S->GetName();
		N.RemoveFromStart(TEXT("Sein"));
		if (N.StartsWith(TEXT("F"))) N.RemoveAt(0);
		if (!N.EndsWith(TEXT("Data"))) N += TEXT(" Data");
		else
		{
			const int32 Idx = N.Len() - 4;
			if (Idx > 0 && N[Idx - 1] != TEXT(' '))
			{
				N.InsertAt(Idx, TEXT(' '));
			}
		}
		return N;
	}
}

void UK2Node_SeinSetComponent::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	UEdGraphPin* WorldPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object,
		UObject::StaticClass(), SeinK2SetCompLocal::PN_WorldContext);
	WorldPin->bHidden = true;

	UEdGraphPin* HandlePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct,
		FSeinEntityHandle::StaticStruct(), SeinK2SetCompLocal::PN_EntityHandle);
	HandlePin->PinFriendlyName = LOCTEXT("EntityHandlePinName", "Entity");

	if (SelectedStruct)
	{
		UEdGraphPin* InPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct,
			SelectedStruct, SeinK2SetCompLocal::PN_InStruct);
		InPin->PinFriendlyName = FText::FromString(SeinK2SetCompLocal::PrettyName(SelectedStruct));
	}
	else
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, nullptr,
			SeinK2SetCompLocal::PN_InStruct);
	}

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, SeinK2SetCompLocal::PN_Success);
}

UEdGraphPin* UK2Node_SeinSetComponent::GetExecPin() const
	{ return FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input); }
UEdGraphPin* UK2Node_SeinSetComponent::GetThenPin() const
	{ return FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output); }
UEdGraphPin* UK2Node_SeinSetComponent::GetWorldContextPin() const
	{ return FindPinChecked(SeinK2SetCompLocal::PN_WorldContext, EGPD_Input); }
UEdGraphPin* UK2Node_SeinSetComponent::GetEntityHandlePin() const
	{ return FindPinChecked(SeinK2SetCompLocal::PN_EntityHandle, EGPD_Input); }
UEdGraphPin* UK2Node_SeinSetComponent::GetInStructPin() const
	{ return FindPinChecked(SeinK2SetCompLocal::PN_InStruct, EGPD_Input); }
UEdGraphPin* UK2Node_SeinSetComponent::GetSuccessPin() const
	{ return FindPinChecked(SeinK2SetCompLocal::PN_Success, EGPD_Output); }

FText UK2Node_SeinSetComponent::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (!SelectedStruct)
	{
		return LOCTEXT("NodeTitle_NoStruct", "Set Sein Component (no struct)");
	}
	if (TitleType == ENodeTitleType::MenuTitle)
	{
		return FText::Format(LOCTEXT("NodeMenuTitle", "Set {0}"),
			FText::FromString(SeinK2SetCompLocal::PrettyName(SelectedStruct)));
	}
	return FText::Format(LOCTEXT("NodeFullTitle", "Set {0}"),
		FText::FromString(SeinK2SetCompLocal::PrettyName(SelectedStruct)));
}

FText UK2Node_SeinSetComponent::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "SeinARTS|Component");
}

FSlateIcon UK2Node_SeinSetComponent::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	return FSlateIconFinder::FindIconForClass(UScriptStruct::StaticClass());
}

FText UK2Node_SeinSetComponent::GetTooltipText() const
{
	if (!SelectedStruct)
	{
		return LOCTEXT("Tooltip_NoStruct",
			"Write a Sein sim component into deterministic storage. Pick a struct from the action menu to type the input pin.");
	}
	return FText::Format(LOCTEXT("Tooltip_WithStruct",
		"Replace this entity's {0} component in deterministic storage (or add it if absent)."),
		FText::FromString(SeinK2SetCompLocal::PrettyName(SelectedStruct)));
}

void UK2Node_SeinSetComponent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (!ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		return;
	}

	TArray<UScriptStruct*> Candidates;
	SeinK2SetCompLocal::GatherCandidateStructs(Candidates);

	for (UScriptStruct* S : Candidates)
	{
		if (!S) continue;

		UBlueprintNodeSpawner* Spawner = UBlueprintNodeSpawner::Create(GetClass());
		if (!Spawner) continue;

		TWeakObjectPtr<UScriptStruct> WeakStruct = S;
		Spawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateLambda(
			[WeakStruct](UEdGraphNode* NewNode, bool /*bIsTemplateNode*/)
			{
				if (UK2Node_SeinSetComponent* TypedNode = Cast<UK2Node_SeinSetComponent>(NewNode))
				{
					TypedNode->SelectedStruct = WeakStruct.Get();
				}
			});

		ActionRegistrar.AddBlueprintAction(ActionKey, Spawner);
	}
}

void UK2Node_SeinSetComponent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	if (!SelectedStruct)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("ExpandNode_NoStruct",
			"@@ has no selected struct — re-spawn the node from the SeinARTS|Component menu.").ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->FunctionReference.SetExternalMember(
		GET_FUNCTION_NAME_CHECKED(USeinComponentBPFL, SeinSetComponentTyped),
		USeinComponentBPFL::StaticClass());
	CallNode->AllocateDefaultPins();

	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *CallNode->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *CallNode->GetThenPin());

	if (UEdGraphPin* CallWorldPin = CallNode->FindPin(SeinK2SetCompLocal::PN_WorldContext))
	{
		CompilerContext.MovePinLinksToIntermediate(*GetWorldContextPin(), *CallWorldPin);
	}

	if (UEdGraphPin* CallHandlePin = CallNode->FindPin(SeinK2SetCompLocal::PN_EntityHandle))
	{
		CompilerContext.MovePinLinksToIntermediate(*GetEntityHandlePin(), *CallHandlePin);
	}

	if (UEdGraphPin* CallInStructPin = CallNode->FindPin(SeinK2SetCompLocal::PN_InStruct))
	{
		CallInStructPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		CallInStructPin->PinType.PinSubCategoryObject = SelectedStruct;
		CompilerContext.MovePinLinksToIntermediate(*GetInStructPin(), *CallInStructPin);
	}

	if (UEdGraphPin* CallReturnPin = CallNode->GetReturnValuePin())
	{
		CompilerContext.MovePinLinksToIntermediate(*GetSuccessPin(), *CallReturnPin);
	}

	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESPACE
