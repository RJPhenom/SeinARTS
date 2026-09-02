/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinEntityComponent.cpp
 * @brief:   Implementation of the data-only authoring component base and the
 *           native Extents concrete. See the header for the two-layer
 *           authoring/runtime contract.
 */

#include "Authoring/SeinEntityComponent.h"

#include "Actor/SeinEntityBridgeComponent.h"
#include "GameFramework/Actor.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h"

#include "SeinARTSCoreEntityLog.h"
DEFINE_LOG_CATEGORY_STATIC(LogSeinDataComponent, Log, All);

USeinEntityComponent::USeinEntityComponent()
{
	// Data-only: never ticks, never registers render/physics state, and is
	// excluded from cooked builds outright (the bridge's baked ComponentData
	// array is the runtime carrier).
	PrimaryComponentTick.bCanEverTick = false;
	bIsEditorOnly = true;
	bAutoActivate = false;
}

const UScriptStruct* USeinEntityComponent::GetPayloadStruct() const
{
	return PayloadStruct;
}

bool USeinEntityComponent::WritePayload(FInstancedStruct& Out) const
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
void USeinEntityComponent::SeedFromPayload(const FInstancedStruct& Entry)
{
	const UScriptStruct* PayloadType = GetPayloadStruct();
	if (!Entry.IsValid() || !PayloadType
		|| Entry.GetScriptStruct() != PayloadType)
	{
		return;
	}
	const USeinEntityComponent* Archetype =
		Cast<USeinEntityComponent>(GetArchetype());

	Modify();

	// Native shape: one embedded struct member of the payload type. Seed its
	// fields individually — archetype-identical fields adopt the entry value,
	// a designer's pre-bake edits survive.
	for (TFieldIterator<FStructProperty> MemberIt(GetClass()); MemberIt; ++MemberIt)
	{
		FStructProperty* Member = *MemberIt;
		if (Member->Struct != PayloadType)
		{
			continue;
		}
		void* MemberValue = Member->ContainerPtrToValuePtr<void>(this);
		const void* ArchetypeValue = Archetype
			? Member->ContainerPtrToValuePtr<const void>(Archetype)
			: nullptr;
		for (TFieldIterator<FProperty> FieldIt(PayloadType); FieldIt; ++FieldIt)
		{
			const FProperty* Field = *FieldIt;
			if (ArchetypeValue
				&& !Field->Identical_InContainer(MemberValue, ArchetypeValue))
			{
				continue;
			}
			Field->CopyCompleteValue(
				Field->ContainerPtrToValuePtr<void>(MemberValue),
				Field->ContainerPtrToValuePtr<const void>(Entry.GetMemory()));
		}
		return;
	}

	// Blueprint shape: payload fields mirror top-level class properties.
	for (TFieldIterator<FProperty> FieldIt(PayloadType); FieldIt; ++FieldIt)
	{
		const FProperty* Field = *FieldIt;
		const FString AuthoredName = PayloadType->GetAuthoredNameForField(Field);
		FProperty* Target = GetClass()->FindPropertyByName(FName(*AuthoredName));
		if (!Target || !Target->SameType(Field))
		{
			continue;
		}
		if (Archetype && !Target->Identical_InContainer(this, Archetype))
		{
			continue;
		}
		Target->CopyCompleteValue(
			Target->ContainerPtrToValuePtr<void>(this),
			Field->ContainerPtrToValuePtr<const void>(Entry.GetMemory()));
	}
}

void USeinEntityComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
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

const UScriptStruct* USeinExtentsComponent::GetPayloadStruct() const
{
	return FSeinExtentsPayload::StaticStruct();
}

bool USeinExtentsComponent::WritePayload(FInstancedStruct& Out) const
{
	Out.InitializeAs<FSeinExtentsPayload>(Extents);
	return true;
}
