/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinDataComponent.cpp
 * @brief:   Implementation of the data-only authoring component base and the
 *           native Extents concrete. See the header for the two-layer
 *           authoring/runtime contract.
 */

#include "Authoring/SeinDataComponent.h"

#include "Actor/SeinEntityBridgeComponent.h"
#include "GameFramework/Actor.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h"

#include "SeinARTSCoreEntityLog.h"
DEFINE_LOG_CATEGORY_STATIC(LogSeinDataComponent, Log, All);

USeinDataComponent::USeinDataComponent()
{
	// Data-only: never ticks, never registers render/physics state, and is
	// excluded from cooked builds outright (the bridge's baked ComponentData
	// array is the runtime carrier).
	PrimaryComponentTick.bCanEverTick = false;
	bIsEditorOnly = true;
	bAutoActivate = false;
}

const UScriptStruct* USeinDataComponent::GetPayloadStruct() const
{
	return PayloadStruct;
}

bool USeinDataComponent::WritePayload(FInstancedStruct& Out) const
{
	const UScriptStruct* Struct = GetPayloadStruct();
	if (!Struct)
	{
		return false;
	}

	// Blueprint path: the paired payload struct's fields mirror this class's
	// Blueprint variables by name and type (SeinDataComponentSync maintains
	// that). Initialize the struct to its authored defaults, then copy every
	// matching property value from this component instance.
	Out.InitializeAs(Struct);
	for (TFieldIterator<FProperty> FieldIt(Struct); FieldIt; ++FieldIt)
	{
		const FProperty* Field = *FieldIt;
		const FString AuthoredName = Struct->GetAuthoredNameForField(Field);
		const FProperty* Source =
			GetClass()->FindPropertyByName(FName(*AuthoredName));
		if (!Source || !Source->SameType(Field))
		{
			UE_LOG(LogSeinDataComponent, Warning,
				TEXT("%s: payload field '%s' has no matching component property (stale sync?); its authored struct default is used."),
				*GetPathName(), *AuthoredName);
			continue;
		}
		Field->CopyCompleteValue(
			Field->ContainerPtrToValuePtr<void>(Out.GetMutableMemory()),
			Source->ContainerPtrToValuePtr<const void>(this));
	}
	return true;
}

#if WITH_EDITOR
void USeinDataComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (IsTemplate())
	{
		// Class-defaults / SCS-template edit: bake the owning class's CDO
		// bridge immediately, through the same bracketed pipeline as a direct
		// ComponentData details edit — so class history, instance propagation,
		// and (during PIE) the class-scope live-tuning command all fire.
		// Waiting for the next compile leaves a window where PIE injects the
		// OLD value while every panel shows the new one.
		const AActor* OwnerActor = GetOwner();
		UClass* OwningClass = OwnerActor
			? OwnerActor->GetClass() : GetTypedOuter<UClass>();
		const AActor* ClassDefault = OwningClass
			? Cast<AActor>(OwningClass->GetDefaultObject(/*bCreateIfNeeded*/ false))
			: nullptr;
		USeinEntityBridgeComponent* Bridge = ClassDefault
			? ClassDefault->FindComponentByClass<USeinEntityBridgeComponent>()
			: nullptr;
		if (Bridge)
		{
			Bridge->NotifyAuthoringComponentEdited(*this);
		}
		return;
	}
	const AActor* Owner = GetOwner();
	USeinEntityBridgeComponent* Bridge =
		Owner ? Owner->FindComponentByClass<USeinEntityBridgeComponent>() : nullptr;
	if (Bridge)
	{
		// The bridge routes this through its ordinary ComponentData edit
		// pipeline: instance-override bookkeeping in the editor world, and
		// the entity-scoped live-tuning command when this is a PIE actor.
		Bridge->NotifyAuthoringComponentEdited(*this);
	}
}
#endif

const UScriptStruct* USeinExtentsDataComponent::GetPayloadStruct() const
{
	return FSeinExtentsPayload::StaticStruct();
}

bool USeinExtentsDataComponent::WritePayload(FInstancedStruct& Out) const
{
	Out.InitializeAs<FSeinExtentsPayload>(Extents);
	return true;
}
