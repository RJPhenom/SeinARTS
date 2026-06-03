/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverComponentDetails.cpp
 */

#include "Details/SeinCoverComponentDetails.h"

#include "Components/SeinCoverComponent.h"
#include "Components/SeinExtentsComponent.h"   // FSeinExtentsShape resolution for Edge mode
#include "Actor/SeinEntityComponent.h"         // walk owning bridge's ComponentData

#include "DetailLayoutBuilder.h"           // IDetailLayoutBuilder::GetDetailFont
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "ScopedTransaction.h"
#include "StructUtils/InstancedStruct.h"

// BP preview-actor refresh + Blueprint editor lookup. The BP editor maintains
// a separate preview actor (NOT the CDO) that the component visualizer draws
// against; without explicitly rebuilding it after a CDO mutation, the
// visualizer keeps drawing stale data. See the OnGenerateButtonClicked body
// for the full call chain (mirrors the pre-refactor pattern in commit 0312e7b).
#include "BlueprintEditor.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/IToolkit.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCoverDetails, Log, All);

#define LOCTEXT_NAMESPACE "SeinCoverComponentDetails"

TSharedRef<IPropertyTypeCustomization> FSeinCoverComponentDetails::MakeInstance()
{
	return MakeShared<FSeinCoverComponentDetails>();
}

void FSeinCoverComponentDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	FDetailWidgetRow& HeaderRow,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	StructHandle = StructPropertyHandle;
	PropUtils = CustomizationUtils.GetPropertyUtilities();

	// Collapse the header row entirely. The FInstancedStruct customization
	// (`FSeinInstancedStructDetails`) that owns the outer "Index [N] → Sein
	// Cover Component" row already labels this entry; a second "Sein Cover
	// Component" header below it is redundant. Empty content alone leaves a
	// visually blank but still-clickable row reserving vertical space;
	// EVisibility::Collapsed removes the row from the layout.
	//
	// Children still render via CustomizeChildren — UE's struct-customization
	// pipeline doesn't gate the children area on header visibility.
	HeaderRow.Visibility(EVisibility::Collapsed);
}

void FSeinCoverComponentDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> StructPropertyHandle,
	IDetailChildrenBuilder& ChildBuilder,
	IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// Render every UPROPERTY of the struct using UE's default layout. Without
	// this loop the struct would show only the header — the children would
	// disappear entirely.
	uint32 NumChildren = 0;
	StructPropertyHandle->GetNumChildren(NumChildren);
	for (uint32 i = 0; i < NumChildren; ++i)
	{
		TSharedPtr<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(i);
		if (ChildHandle.IsValid())
		{
			ChildBuilder.AddProperty(ChildHandle.ToSharedRef());
		}
	}

	// Generate Slots button — appended below the per-field rows. UE puts
	// custom rows in their declaration order, so this lands at the bottom
	// of the struct's expanded view. Designer flow: configure GenerateMode +
	// SlotCount + Inset + Area, then click Generate.
	ChildBuilder.AddCustomRow(LOCTEXT("GenerateSlotsRowFilter", "Generate Slots"))
		.NameContent()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("GenerateSlotsLabel", "Generate Slots"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(180.0f)
		[
			SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ContentPadding(FMargin(16.0f, 4.0f))
				.ToolTipText(LOCTEXT("GenerateSlotsTooltip",
					"Replace the Slots array with procedurally-distributed slots derived from "
					"GenerateMode + Area. Edge mode requires Area.Shape == Box; Area mode requires "
					"Area.Shape != None. Designers can edit individual slots afterward — this is "
					"a starting-point convenience, not a runtime helper."))
				.OnClicked(FOnClicked::CreateSP(this, &FSeinCoverComponentDetails::OnGenerateButtonClicked))
				[
					SNew(STextBlock)
						.Text(LOCTEXT("GenerateSlotsButton", "Generate"))
				]
		];
}

namespace
{
	/** Resolve the first Box-shaped FSeinExtentsComponent entry on the
	 *  passed-in bridge's ComponentData array. Returns nullptr if no
	 *  Extents entry exists, or it has no Shapes, or the first Box is
	 *  missing. Used by Edge-mode generation to find the wall body geometry
	 *  the slots wrap around.
	 *
	 *  Walks Shapes in order and returns the first Box — designers who want
	 *  a specific shape's body for cover should put that Box first (or
	 *  use a separate Extents entry per body). */
	static const FSeinExtentsShape* ResolveFirstBoxShape(const USeinEntityComponent* Bridge)
	{
		if (!Bridge) return nullptr;
		for (const FInstancedStruct& Entry : Bridge->ComponentData)
		{
			if (!Entry.IsValid()) continue;
			if (Entry.GetScriptStruct() != FSeinExtentsComponent::StaticStruct()) continue;
			const FSeinExtentsComponent& Extents = Entry.Get<FSeinExtentsComponent>();
			for (const FSeinExtentsShape& Shape : Extents.Shapes)
			{
				if (Shape.Shape == ESeinExtentsShape::Box)
				{
					return &Shape;
				}
			}
		}
		return nullptr;
	}
}

FReply FSeinCoverComponentDetails::OnGenerateButtonClicked()
{
	// =============================================================================
	// Replicate the EXACT property-edit pipeline UE uses for manual array
	// edits (the "+" button on the Slots array). The user observation that
	// drove this approach: manually adding a slot in the array UI propagates
	// correctly to placed instances + the BP preview viewport; only the
	// programmatic Generate button was failing.
	//
	// Why the old approach failed: calling `Comp->GenerateSlots(Body)` on
	// raw memory bypasses UE's property-editor propagation entirely. The
	// stock chain notifications (`PostEditChangeChainProperty`) only
	// NOTIFY archetype instances — they do NOT copy values. The actual
	// value-mirroring lives in `FPropertyNode::PropagatePropertyChange`
	// (PropertyEditor/Private/PropertyNode.cpp:4123), which is invoked
	// internally when the property editor's ImportText path runs.
	//
	// The canonical fix: drive the change through `IPropertyHandle::
	// SetPerObjectValues` on the Slots child handle. That method:
	//   - Resolves per-outer addresses
	//   - Calls `FPropertyValueImpl::ImportText` (one value per outer)
	//   - ImportText fires PreEditChange, mutates the CDO via
	//     `Property->ImportText_Direct`, calls `PropagatePropertyChange`
	//     (which mirrors the value to instances whose current value
	//     matches the pre-edit CDO value — preserves overrides), fires
	//     PostEditChangeProperty, marks packages dirty.
	//
	// The whole thing collapses into:
	//   1. Get the Slots child handle.
	//   2. For each outer: clone the current CDO struct, run GenerateSlots
	//      against the clone (which uses the outer's body shape), export
	//      the clone's Slots array to a formatted string.
	//   3. SlotsHandle->SetPerObjectValues(perOuterStrings).
	//   4. UpdatePreviewActor(BP, true) per BP so the BP editor's preview
	//      actor rebuilds against the new CDO.
	//   5. RedrawAllViewports + ForceRefresh on the details panel.
	// =============================================================================
	if (!StructHandle.IsValid() || !StructHandle->IsValidHandle())
	{
		UE_LOG(LogSeinCoverDetails, Warning,
			TEXT("[OnGenerateButtonClicked] Struct handle invalid — bailing"));
		return FReply::Handled();
	}

	// Resolve the Slots child handle. It's the SINGLE property that needs
	// the change driven through UE's pipeline; everything else on the
	// struct (QualityTag, bIsDirectional, Area) stays at its current value.
	const TSharedPtr<IPropertyHandle> SlotsHandle = StructHandle->GetChildHandle(
		GET_MEMBER_NAME_CHECKED(FSeinCoverComponent, Slots));
	if (!SlotsHandle.IsValid() || !SlotsHandle->IsValidHandle())
	{
		UE_LOG(LogSeinCoverDetails, Warning,
			TEXT("[OnGenerateButtonClicked] Could not resolve Slots child handle — bailing"));
		return FReply::Handled();
	}

	FArrayProperty* SlotsProp = CastField<FArrayProperty>(SlotsHandle->GetProperty());
	if (!SlotsProp)
	{
		UE_LOG(LogSeinCoverDetails, Warning,
			TEXT("[OnGenerateButtonClicked] Slots handle is not an array property — bailing"));
		return FReply::Handled();
	}

	FScopedTransaction Transaction(LOCTEXT("GenerateCoverSlots", "Generate Cover Slots"));

	UE_LOG(LogSeinCoverDetails, Log, TEXT("[OnGenerateButtonClicked] FIRED"));

	// Resolve per-outer body shapes + collect each outer's owning Blueprint
	// (for the post-set preview rebuild pass).
	TArray<UObject*> OuterObjects;
	StructHandle->GetOuterObjects(OuterObjects);

	TArray<const FSeinExtentsShape*> BodyShapesByOuter;
	BodyShapesByOuter.Reserve(OuterObjects.Num());
	TSet<UBlueprint*> ModifiedBlueprints;
	for (UObject* Outer : OuterObjects)
	{
		const USeinEntityComponent* Bridge = Cast<USeinEntityComponent>(Outer);
		BodyShapesByOuter.Add(ResolveFirstBoxShape(Bridge));

		if (UClass* OwnerClass = Outer ? Outer->GetTypedOuter<UClass>() : nullptr)
		{
			if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(OwnerClass))
			{
				if (UBlueprint* BP = Cast<UBlueprint>(BPGC->ClassGeneratedBy))
				{
					ModifiedBlueprints.Add(BP);
				}
			}
		}
	}

	// Build the per-outer formatted Slots-array string. For each outer:
	//   - Clone the current CDO struct (so QualityTag/etc. used by
	//     GenerateSlots's internal logic are correct per outer).
	//   - Call GenerateSlots(Body) on the clone — this populates Slots.
	//   - Export the clone's Slots array to a UE property text string via
	//     FArrayProperty::ExportText_Direct.
	TArray<FString> PerOuterValues;
	PerOuterValues.SetNum(OuterObjects.Num());

	int32 GeneratedFor = 0;
	StructHandle->EnumerateRawData(
		[&PerOuterValues, &BodyShapesByOuter, SlotsProp, &GeneratedFor]
		(void* RawData, const int32 DataIndex, const int32 /*NumDatas*/)
		{
			const FSeinCoverComponent* Source = static_cast<const FSeinCoverComponent*>(RawData);
			if (!Source) return true;

			// Clone — explicit copy of the source CDO struct so its existing
			// fields drive GenerateSlots's behavior (Area, mode, count, etc).
			FSeinCoverComponent Clone = *Source;
			const FSeinExtentsShape* Body = BodyShapesByOuter.IsValidIndex(DataIndex)
				? BodyShapesByOuter[DataIndex] : nullptr;
			Clone.GenerateSlots(Body);

			// Export the new Slots array to a formatted string. UE's
			// SetPerObjectValues + ImportText path will re-import this
			// string into each outer's Slots property, going through
			// PropagatePropertyChange for archetype-instance mirroring.
			FString SlotsText;
			SlotsProp->ExportText_Direct(SlotsText, &Clone.Slots, &Clone.Slots, nullptr, PPF_None);
			PerOuterValues[DataIndex] = MoveTemp(SlotsText);

			UE_LOG(LogSeinCoverDetails, Log,
				TEXT("[OnGenerateButtonClicked]   Outer #%d: generated %d slot(s), BodyBox=%s, text-len=%d"),
				DataIndex, Clone.Slots.Num(),
				Body ? TEXT("yes") : TEXT("no"),
				PerOuterValues[DataIndex].Len());

			++GeneratedFor;
			return true;
		});

	UE_LOG(LogSeinCoverDetails, Log,
		TEXT("[OnGenerateButtonClicked] Built per-outer values for %d outer(s)"), GeneratedFor);

	// THE CRITICAL CALL. UE handles everything from here:
	//   - PreEditChange on each outer (with the right chain)
	//   - ImportText_Direct mutates the CDO's Slots
	//   - PropagatePropertyChange mirrors to archetype instances (with
	//     text-Identical override check)
	//   - PostEditChangeProperty + PostEditChangeChainProperty fired
	//     with full chain info (handle's parent chain is intact)
	//   - Package dirty flags + transaction tracking
	const FPropertyAccess::Result Result = SlotsHandle->SetPerObjectValues(PerOuterValues);
	if (Result != FPropertyAccess::Success)
	{
		UE_LOG(LogSeinCoverDetails, Warning,
			TEXT("[OnGenerateButtonClicked] SetPerObjectValues returned non-success (%d) — propagation may be incomplete"),
			(int32)Result);
	}
	else
	{
		UE_LOG(LogSeinCoverDetails, Log,
			TEXT("[OnGenerateButtonClicked] SetPerObjectValues succeeded — UE handled propagation"));
	}

	// BP preview-actor rebuild. The BP editor maintains a SEPARATE preview
	// actor in a hidden preview world; the component visualizer draws
	// against that instance. `UpdatePreviewActor(BP, true)` destroys +
	// respawns it so its bridge picks up the new CDO state
	// (BlueprintEditor.cpp:10491-10556 in UE 5.7).
	if (GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			for (UBlueprint* BP : ModifiedBlueprints)
			{
				if (!BP) continue;
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

				for (IAssetEditorInstance* EditorInst : AssetEditorSubsystem->FindEditorsForAsset(BP))
				{
					if (!EditorInst) continue;
					if (EditorInst->GetEditorName() == TEXT("BlueprintEditor"))
					{
						FBlueprintEditor* BPEditor = static_cast<FBlueprintEditor*>(EditorInst);
						BPEditor->UpdatePreviewActor(BP, /*bInForceFullUpdate*/ true);
						UE_LOG(LogSeinCoverDetails, Log,
							TEXT("[OnGenerateButtonClicked] UpdatePreviewActor(force=true) fired for BP %s"),
							*BP->GetName());
					}
				}
			}
		}

		GEditor->RedrawAllViewports(/*bInvalidateHitProxies*/ true);
	}

	// Details-panel reflow so the new Slots count shows immediately.
	if (PropUtils.IsValid())
	{
		PropUtils->ForceRefresh();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
