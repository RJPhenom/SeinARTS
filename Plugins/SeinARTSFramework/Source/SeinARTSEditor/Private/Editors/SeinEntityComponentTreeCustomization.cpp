/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinEntityComponentTreeCustomization.cpp
 * @author       RJ Macklem
 * @created      4 Sep 2026
 * @latest       4 Sep 2026
 * @brief        Implements the editor-only entity-component tree grouping.
 *
 *               The stock subobject editor builds a flat set of non-scene
 *               components. After that rebuild completes, this customization
 *               reparents only its Slate wrapper nodes under Entity Bridge.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Editors/SeinEntityComponentTreeCustomization.h"

#include "Actor/SeinActor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Authoring/SeinEntityComponent.h"
#include "Engine/Blueprint.h"
#include "SubobjectData.h"
#include "SSubobjectEditor.h"

namespace
{
	using FTreeNodePtr = FSubobjectEditorTreeNodePtrType;

	bool IsSeinActorBlueprint(const SSubobjectEditor& SubobjectEditor)
	{
		const UBlueprint* Blueprint = SubobjectEditor.GetBlueprint();
		return Blueprint != nullptr
			&& Blueprint->ParentClass != nullptr
			&& Blueprint->ParentClass->IsChildOf(ASeinActor::StaticClass());
	}

	void GatherTreeNodes(
		const FTreeNodePtr& Node,
		TArray<FTreeNodePtr>& OutNodes)
	{
		if (!Node.IsValid())
		{
			return;
		}

		OutNodes.Add(Node);
		for (const FTreeNodePtr& Child : Node->GetChildren())
		{
			GatherTreeNodes(Child, OutNodes);
		}
	}

	bool IsEntityAuthoringNode(const FTreeNodePtr& Node)
	{
		const UActorComponent* Component = Node.IsValid()
			? Node->GetComponentTemplate()
			: nullptr;
		return Component != nullptr
			&& Component->IsA<USeinEntityComponent>();
	}
}

FSeinEntityComponentTreeCustomization::~FSeinEntityComponentTreeCustomization()
{
	CancelPendingWork();
}

TSharedPtr<SWidget>
FSeinEntityComponentTreeCustomization::GetControlsWidget(
	TSharedRef<SSubobjectEditor>& SubobjectEditor,
	const FSubobjectData& /*Data*/) const
{
	if (bActive && IsSeinActorBlueprint(*SubobjectEditor))
	{
		ScheduleGrouping(SubobjectEditor);
	}

	// Returning null preserves Unreal's normal per-row controls.
	return nullptr;
}

void FSeinEntityComponentTreeCustomization::Deactivate()
{
	bActive = false;
	CancelPendingWork();
}

void FSeinEntityComponentTreeCustomization::RefreshTrackedEditors()
{
	for (const TWeakPtr<SSubobjectEditor>& WeakEditor : TrackedEditors)
	{
		if (const TSharedPtr<SSubobjectEditor> Editor = WeakEditor.Pin())
		{
			Editor->UpdateTree(true);
		}
	}
	TrackedEditors.Reset();
}

void FSeinEntityComponentTreeCustomization::ScheduleGrouping(
	TSharedRef<SSubobjectEditor> SubobjectEditor) const
{
	const SSubobjectEditor* EditorKey = &SubobjectEditor.Get();
	TrackedEditors.RemoveAll(
		[](const TWeakPtr<SSubobjectEditor>& Editor)
		{
			return !Editor.IsValid();
		});
	if (!TrackedEditors.ContainsByPredicate(
		[EditorKey](const TWeakPtr<SSubobjectEditor>& Editor)
		{
			const TSharedPtr<SSubobjectEditor> Pinned = Editor.Pin();
			return Pinned.Get() == EditorKey;
		}))
	{
		TrackedEditors.Add(SubobjectEditor);
	}

	if (PendingEditors.Contains(EditorKey))
	{
		return;
	}

	const TWeakPtr<const FSeinEntityComponentTreeCustomization> WeakThis =
		AsShared();
	const TWeakPtr<SSubobjectEditor> WeakEditor = SubobjectEditor;
	const FTSTicker::FDelegateHandle Handle =
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[WeakThis, WeakEditor, EditorKey](float)
				{
					if (const TSharedPtr<const FSeinEntityComponentTreeCustomization>
						Customization = WeakThis.Pin())
					{
						Customization->PendingEditors.Remove(EditorKey);
						if (Customization->bActive)
						{
							if (const TSharedPtr<SSubobjectEditor> Editor =
								WeakEditor.Pin())
							{
								Customization->ApplyGrouping(*Editor);
							}
						}
					}
					return false;
				}));
	PendingEditors.Add(EditorKey, Handle);
}

bool FSeinEntityComponentTreeCustomization::ApplyGrouping(
	SSubobjectEditor& SubobjectEditor) const
{
	if (!IsSeinActorBlueprint(SubobjectEditor))
	{
		return false;
	}

	TArray<FTreeNodePtr> Nodes;
	for (const FTreeNodePtr& Root : SubobjectEditor.GetRootNodes())
	{
		GatherTreeNodes(Root, Nodes);
	}

	FTreeNodePtr BridgeNode;
	TArray<FTreeNodePtr> EntityNodes;
	for (const FTreeNodePtr& Node : Nodes)
	{
		const UActorComponent* Component = Node.IsValid()
			? Node->GetComponentTemplate()
			: nullptr;
		if (Component == nullptr)
		{
			continue;
		}

		if (Component->IsA<USeinEntityBridgeComponent>())
		{
			// ASeinActor's native bridge wins if a malformed Blueprint also owns
			// an additional bridge component.
			if (!BridgeNode.IsValid() || Node->IsNativeComponent())
			{
				BridgeNode = Node;
			}
		}
		else if (IsEntityAuthoringNode(Node))
		{
			EntityNodes.Add(Node);
		}
	}

	if (!BridgeNode.IsValid() || EntityNodes.IsEmpty())
	{
		return false;
	}

	EntityNodes.Sort(
		[](const FTreeNodePtr& Left, const FTreeNodePtr& Right)
		{
			return Left->GetDisplayString() < Right->GetDisplayString();
		});

	TArray<FTreeNodePtr> CurrentEntityChildren;
	for (const FTreeNodePtr& Child : BridgeNode->GetChildren())
	{
		if (IsEntityAuthoringNode(Child))
		{
			CurrentEntityChildren.Add(Child);
		}
	}

	bool bAlreadyGrouped =
		CurrentEntityChildren.Num() == EntityNodes.Num();
	if (bAlreadyGrouped)
	{
		for (int32 Index = 0; Index < EntityNodes.Num(); ++Index)
		{
			if (CurrentEntityChildren[Index] != EntityNodes[Index]
				|| EntityNodes[Index]->GetParent() != BridgeNode)
			{
				bAlreadyGrouped = false;
				break;
			}
		}
	}

	if (bAlreadyGrouped)
	{
		return false;
	}

	for (const FTreeNodePtr& Child : CurrentEntityChildren)
	{
		BridgeNode->RemoveChild(Child);
	}
	for (const FTreeNodePtr& EntityNode : EntityNodes)
	{
		BridgeNode->AddChild(EntityNode);
	}

	// Recompute the cached search-filter ancestry after changing only the
	// visual tree. This lets a matching child keep its bridge row visible.
	BridgeNode->RefreshCachedChildFilterState(true);
	if (const FTreeNodePtr BridgeParent = BridgeNode->GetParent())
	{
		BridgeParent->RefreshCachedChildFilterState(true);
	}

	if (const TSharedPtr<SSubobjectEditorDragDropTree> Tree =
		SubobjectEditor.GetDragDropTree())
	{
		Tree->SetItemExpansion(BridgeNode, true);
		Tree->RequestTreeRefresh();
	}
	return true;
}

void FSeinEntityComponentTreeCustomization::CancelPendingWork()
{
	for (const TPair<const SSubobjectEditor*, FTSTicker::FDelegateHandle>& Pair :
		PendingEditors)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Pair.Value);
	}
	PendingEditors.Reset();
}
