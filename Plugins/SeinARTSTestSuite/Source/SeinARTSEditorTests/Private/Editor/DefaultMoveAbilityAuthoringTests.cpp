/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         DefaultMoveAbilityAuthoringTests.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Validates live and inherited Blueprint grants used by the movement picker.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "CQTest.h"
#include "Actor/SeinActor.h"
#include "Authoring/SeinEntityComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Util/SeinDefaultMoveAbilityAuthoring.h"
#include "UObject/Package.h"

TEST(PickerReadsLiveSiblingGrantsAndRejectsStaleSelections, "SeinARTS.Editor.DefaultMove")
{
    TArray<UBlueprint*> Blueprints;
    ON_SCOPE_EXIT
    {
        for (auto* Blueprint : Blueprints)
        {
            Blueprint->ClearFlags(RF_Public | RF_Standalone);
            Blueprint->SetFlags(RF_Transient);
            Blueprint->GetOutermost()->SetDirtyFlag(false);
        }
    };
    auto MakeBlueprint = [&](UClass* Parent, const TCHAR* Name)
    {
        UPackage* Package = CreatePackage(*FString::Printf(TEXT("/SeinARTSTestSuite/%s"), Name));
        auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(Name),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        Blueprints.Add(Blueprint);
        return Blueprint;
    };
    auto* AbilityBP = MakeBlueprint(USeinAbility::StaticClass(), TEXT("DefaultMoveAuthoringAbility"));
    FKismetEditorUtilities::CompileBlueprint(AbilityBP);
    auto* AbilityCDO = AbilityBP->GeneratedClass->GetDefaultObject<USeinAbility>();
    AbilityCDO->TargetType = ESeinAbilityTargetType::Point;
    AbilityCDO->AbilityTag = SeinARTSTags::Command_Context_Target_Ground;
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::IsEligibleClass(AbilityBP->GeneratedClass)));
    auto* EntityBP = MakeBlueprint(ASeinActor::StaticClass(), TEXT("DefaultMoveAuthoringEntity"));
    auto* AbilitiesNode = EntityBP->SimpleConstructionScript->CreateNode(USeinAbilitiesComponent::StaticClass(), TEXT("Abilities"));
    auto* MovementNode = EntityBP->SimpleConstructionScript->CreateNode(USeinMovementComponent::StaticClass(), TEXT("Movement"));
    EntityBP->SimpleConstructionScript->AddNode(AbilitiesNode);
    EntityBP->SimpleConstructionScript->AddNode(MovementNode);
    auto* Abilities = CastChecked<USeinAbilitiesComponent>(AbilitiesNode->ComponentTemplate);
    auto* Movement = CastChecked<USeinMovementComponent>(MovementNode->ComponentTemplate);
    Abilities->Abilities.GrantedAbilities = {AbilityBP->GeneratedClass.Get()};
    Movement->Movement.DefaultMoveAbility = AbilityBP->GeneratedClass.Get();
    FKismetEditorUtilities::CompileBlueprint(EntityBP);
    Abilities = CastChecked<USeinAbilitiesComponent>(AbilitiesNode->ComponentTemplate);
    Movement = CastChecked<USeinMovementComponent>(MovementNode->ComponentTemplate);
    FText Error;
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::ValidateEntity(*EntityBP, Error)));
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::GetGrantedClasses(*Movement).Contains(AbilityBP->GeneratedClass.Get())));
    auto* Child = MakeBlueprint(EntityBP->GeneratedClass, TEXT("DefaultMoveAuthoringChild"));
    FKismetEditorUtilities::CompileBlueprint(Child);
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::ValidateEntity(*Child, Error)));
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::GetGrantedClasses(*Child).Contains(AbilityBP->GeneratedClass.Get())));
    // No compile or payload rebake: picker and validation must see edits now.
    Abilities->Abilities.GrantedAbilities.Reset();
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::GetGrantedClasses(*Movement).IsEmpty()));
    ASSERT_THAT(IsFalse(SeinDefaultMoveAbilityAuthoring::ValidateEntity(*EntityBP, Error)));
    ASSERT_THAT(IsFalse(SeinDefaultMoveAbilityAuthoring::ValidateEntity(*Child, Error)));
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::ValidateSelection(*Movement, nullptr, Error)));
    Abilities->Abilities.GrantedAbilities = {AbilityBP->GeneratedClass.Get()};
    Abilities->bInjectionEnabled = false;
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::GetGrantedClasses(*Movement).IsEmpty()));
    Abilities->bInjectionEnabled = true;
    AbilityCDO->bIsPassive = true;
    ASSERT_THAT(IsFalse(SeinDefaultMoveAbilityAuthoring::ValidateEntity(*EntityBP, Error)));
    AbilityCDO->bIsPassive = false;
    AbilityCDO->TargetType = ESeinAbilityTargetType::None;
    ASSERT_THAT(IsFalse(SeinDefaultMoveAbilityAuthoring::ValidateEntity(*EntityBP, Error)));
    AbilityCDO->TargetType = ESeinAbilityTargetType::Point;
    AbilityCDO->AbilityTag = FGameplayTag();
    ASSERT_THAT(IsFalse(SeinDefaultMoveAbilityAuthoring::ValidateEntity(*EntityBP, Error)));
    AbilityCDO->AbilityTag = SeinARTSTags::Command_Context_Target_Ground;
    // Selecting inherited SCS components in Blueprint Details creates these
    // child-owned override templates. Read through that actual context.
    auto* Handler = Child->GetInheritableComponentHandler(true);
    auto* ChildMovement = CastChecked<USeinMovementComponent>(
        Handler->CreateOverridenComponentTemplate(FComponentKey(MovementNode)));
    auto* ChildAbilities = CastChecked<USeinAbilitiesComponent>(
        Handler->CreateOverridenComponentTemplate(FComponentKey(AbilitiesNode)));
    ChildAbilities->Abilities.GrantedAbilities.Reset();
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::GetGrantedClasses(*ChildMovement).IsEmpty()));
    ASSERT_THAT(IsFalse(SeinDefaultMoveAbilityAuthoring::ValidateSelection(*ChildMovement, AbilityBP->GeneratedClass, Error)));
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::ValidateEntity(*EntityBP, Error)));
    ChildAbilities->Abilities.GrantedAbilities = {AbilityBP->GeneratedClass.Get()};
    ASSERT_THAT(IsTrue(SeinDefaultMoveAbilityAuthoring::ValidateSelection(*ChildMovement, AbilityBP->GeneratedClass, Error)));
}
