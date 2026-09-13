/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinDefaultMoveAbilityAuthoring.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Reads live authoring grants without mutating Blueprint templates or saved payloads.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Util/SeinDefaultMoveAbilityAuthoring.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Authoring/SeinEntityComponent.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"

TArray<UClass*> SeinDefaultMoveAbilityAuthoring::GetGrantedClasses(const UObject& Context)
{
	TArray<UClass*> Result;
	const AActor* Actor = Cast<AActor>(&Context);
	if (!Actor) Actor = Context.GetTypedOuter<AActor>();
	const UBlueprint* Blueprint = Cast<UBlueprint>(&Context);
	if (!Blueprint) Blueprint = Context.GetTypedOuter<UBlueprint>();
	UClass* Class = Actor ? Actor->GetClass() : Context.GetTypedOuter<UClass>();
	if (Blueprint) Class = Blueprint->GeneratedClass;
	TArray<const USeinAbilitiesComponent*> Components;
	if (Actor && !Actor->IsTemplate())
	{
		TArray<USeinAbilitiesComponent*> Instances;
		Actor->GetComponents(Instances);
		for (const auto* Component : Instances) Components.Add(Component);
	}
	else if (Class)
	{
		AActor::GetActorClassDefaultComponents<USeinAbilitiesComponent>(Class, Components);
	}
	for (const auto* Component : Components)
		if (Component && Component->bInjectionEnabled)
			for (const auto& Ability : Component->Abilities.GrantedAbilities)
				if (Ability) Result.AddUnique(Ability.Get());
	// An authored component, including a disabled one, owns the payload. Never
	// expose stale baked grants while its current authoring array is empty.
	if (!Components.IsEmpty()) return Result;
	if (!Actor && Class) Actor = Cast<AActor>(Class->GetDefaultObject(false));
	const auto* Bridge = Actor ? Actor->FindComponentByClass<USeinEntityBridgeComponent>() : nullptr;
	if (Bridge)
		for (const auto& Entry : Bridge->ComponentData)
			if (const auto* Abilities = Entry.GetPtr<FSeinAbilityPayload>())
				for (const auto& Ability : Abilities->GrantedAbilities)
					if (Ability) Result.AddUnique(Ability.Get());
	return Result;
}

bool SeinDefaultMoveAbilityAuthoring::IsEligibleClass(const UClass* Class)
{
	if (!Class || !Class->IsChildOf(USeinAbility::StaticClass())
		|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) return false;
	const auto* Ability = Cast<USeinAbility>(Class->GetDefaultObject());
	return Ability && !Ability->bIsPassive && Ability->TargetType == ESeinAbilityTargetType::Point
		&& Ability->AbilityTag.IsValid();
}

bool SeinDefaultMoveAbilityAuthoring::ValidateSelection(const UObject& Context, const UClass* Class, FText& Error)
{
	Error = FText::GetEmpty();
	if (!Class) return true;
	const TArray<UClass*> Grants = GetGrantedClasses(Context);
	if (!Grants.Contains(Class))
		Error = FText::FromString(TEXT("Movement Ability must select an ability in this entity's Granted Abilities. Grant it first, choose another granted ability, or clear the selection."));
	else if (!IsEligibleClass(Class))
		Error = FText::FromString(TEXT("Movement Ability must be a concrete, non-passive Point ability with a valid Ability Tag."));
	else
	{
		const auto* Selected = Class->GetDefaultObject<USeinAbility>();
		for (const auto* Grant : Grants)
		{
			const auto* Other = Grant->GetDefaultObject<USeinAbility>();
			if (Grant != Class && Other && Other->AbilityTag == Selected->AbilityTag)
			{
				Error = FText::FromString(TEXT("Movement Ability shares its Ability Tag with another granted class. Give each granted ability a unique tag so automatic orders resolve to the selected class."));
				break;
			}
		}
	}
	return Error.IsEmpty();
}

bool SeinDefaultMoveAbilityAuthoring::ValidateEntity(const UObject& Context, FText& Error)
{
	Error = FText::GetEmpty();
	const UBlueprint* Blueprint = Cast<UBlueprint>(&Context);
	const AActor* Actor = Blueprint && Blueprint->GeneratedClass
		? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)) : Cast<AActor>(&Context);
	if (!Actor) return true;
	TArray<const USeinMovementComponent*> Components;
	if (Actor->IsTemplate())
		AActor::GetActorClassDefaultComponents<USeinMovementComponent>(Actor->GetClass(), Components);
	else
	{
		TArray<USeinMovementComponent*> Instances;
		Actor->GetComponents(Instances);
		for (const auto* Component : Instances) Components.Add(Component);
	}
	for (const auto* Component : Components)
		if (Component && Component->bInjectionEnabled
			&& !ValidateSelection(Context, Component->Movement.DefaultMoveAbility, Error)) return false;
	if (!Components.IsEmpty()) return true;
	if (const auto* Bridge = Actor->FindComponentByClass<USeinEntityBridgeComponent>())
		for (const auto& Entry : Bridge->ComponentData)
			if (const auto* Movement = Entry.GetPtr<FSeinMovementPayload>())
				if (!ValidateSelection(Context, Movement->DefaultMoveAbility, Error)) return false;
	return true;
}
