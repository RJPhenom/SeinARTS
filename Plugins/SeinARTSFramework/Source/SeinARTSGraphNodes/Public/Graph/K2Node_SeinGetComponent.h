/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    K2Node_SeinGetComponent.h
 * @brief:   Blueprint editor node for typed component access. Designer picks
 *           a sim-component struct type (FSeinMovementComponent,
 *           or a designer-authored UDS marked SeinDeterministic) from the
 *           BP action menu, gets back a "Get <StructName> Data" node with a
 *           pre-typed output pin that's directly Break-Struct compatible.
 *
 *           Wraps `USeinComponentBPFL::SeinGetComponentTyped` (CustomThunk).
 *           Generates the call at compile time with the user-selected struct
 *           as the wildcard CustomStructureParam — Blueprint sees a strongly
 *           typed struct output and standard tooling (Break Struct, member
 *           lookup, etc.) works without any FInstancedStruct unwrap step.
 *
 *           One menu action per registered FSeinComponent substruct keeps
 *           discoverability high: the user types "Get Combat Data" in the
 *           action menu and the right node lands.
 */

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "UObject/ObjectMacros.h"
#include "K2Node_SeinGetComponent.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;

UCLASS()
class SEINARTSGRAPHNODES_API UK2Node_SeinGetComponent : public UK2Node
{
	GENERATED_BODY()

public:
	/** Struct type the user picked from the action menu — types the output pin.
	 *  Serialized so the node remembers its type across BP saves; mutated only
	 *  by menu-action spawn (never by the runtime BP author after placement).
	 *  Re-pick by deleting the node and adding a new "Get <X> Data" entry. */
	UPROPERTY()
	TObjectPtr<UScriptStruct> SelectedStruct = nullptr;

	// ===== UEdGraphNode =====
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetMenuCategory() const override;
	virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual FText GetTooltipText() const override;

	// ===== UK2Node =====
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual bool IsNodePure() const override { return false; }
	virtual ERedirectType DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex,
		const UEdGraphPin* OldPin, int32 OldPinIndex) const override;

	// ===== Pin accessors =====
	UEdGraphPin* GetExecPin() const;
	UEdGraphPin* GetThenPin() const;
	UEdGraphPin* GetWorldContextPin() const;
	UEdGraphPin* GetEntityHandlePin() const;
	UEdGraphPin* GetOutStructPin() const;
	UEdGraphPin* GetSuccessPin() const;
};
