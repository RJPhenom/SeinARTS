#include "Classes/Units/SAFUnit.h"
#include "Components/SAFProductionComponent.h"
#include "Assets/Units/SAFUnitAsset.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Engine/ActorChannel.h"
#include "Gameplay/Abilities/SAFAbility.h"
#include "Gameplay/Attributes/SAFAttributeSet.h"
#include "Gameplay/Attributes/SAFUnitAttributes.h"
#include "Gameplay/Attributes/SAFProductionAttributes.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameState.h"
#include "GameplayTagContainer.h"
#include "Net/UnrealNetwork.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Utils/SAFLibrary.h"
#include "Debug/SAFDebugTool.h"
#include "DrawDebugHelpers.h"

ASAFUnit::ASAFUnit() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);
	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

bool ASAFUnit::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags) {
	bool Wrote = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
	if (ProductionComponent) Wrote |= Channel->ReplicateSubobject(ProductionComponent, *Bunch, *RepFlags);
	return Wrote;
}

void ASAFUnit::GetSubobjectsWithStableNamesForNetworking(TArray<UObject*>& Objs) {
	Super::GetSubobjectsWithStableNamesForNetworking(Objs);
	if (ProductionComponent) {
		Objs.Add(ProductionComponent);
	}
}

void ASAFUnit::OnSubobjectCreatedFromReplication(UObject* NewSubobject) {
	Super::OnSubobjectCreatedFromReplication(NewSubobject);

	if (UActorComponent* NewComp = Cast<UActorComponent>(NewSubobject)) {
		if (!NewComp->GetOwner()) NewComp->Rename(nullptr, this);
		if (!NewComp->IsRegistered()) { 
			AddInstanceComponent(NewComp); 
			NewComp->RegisterComponent(); 
		}
	}

	if (USAFProductionComponent* NewProd = Cast<USAFProductionComponent>(NewSubobject)) ProductionComponent = NewProd;
}

// Asset Interface Overrides
// ===============================================================================================================================
void ASAFUnit::SetAsset_Implementation(USAFAsset* InAsset) {
	USAFUnitAsset* UnitAsset = Cast<USAFUnitAsset>(InAsset);
	if (!UnitAsset) { SAFDEBUG_WARNING("SetAsset: unit implementation aborted, asset type was not SAFUnitAsset."); return; }
	
	Asset = InAsset;
	ASAFPlayerState* InOwner = ISAFActorInterface::Execute_GetOwningPlayer(this);
	ISAFActorInterface::Execute_InitFromAsset(this, InAsset, InOwner, true);
}

void ASAFUnit::InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) {
	if (!HasAuthority()) return;

	// Type validation + Super
	USAFAsset* InitAsset = InAsset ? InAsset : SAFAssetResolver::ResolveAsset(Asset);
	USAFUnitAsset* UnitAsset = Cast<USAFUnitAsset>(InitAsset);
	if (!UnitAsset) { SAFDEBUG_WARNING(FORMATSTR("InitFromAsset: invalid Data Asset Type on actor '%s'. Culling.", *GetName())); Destroy(); return; }
	Super::InitFromAsset_Implementation(InAsset, InOwner, bReinitialize);

	// GAS
	InitAbilitySystem();
	GiveTagsFromAsset();
	GiveAttributesFromAsset();
	GiveAbilitiesFromAsset();
	ApplyStartupEffects();

	// Production
	if (GetUnitAsset()->bCanEverProduce && !ProductionComponent.Get()) {
		ProductionComponent = NewObject<USAFProductionComponent>(this, TEXT("ProductionComponent"));
		ProductionComponent->SetIsReplicated(true);
		AddInstanceComponent(ProductionComponent);
		ProductionComponent->RegisterComponent();
		ProductionComponent->InitProductionCatalogue(GetUnitAsset()->ProductionRecipes);
	}

	ForceNetUpdate();
}

// Unit Interface Overrides
// ==================================================================================================
void ASAFUnit::AttachToPawn_Implementation(APawn* Pawn) {
	if (!HasAuthority()) return;
	if (!IsValid(Pawn)) { SAFDEBUG_WARNING("AttachToPawn aborted: invalid actor."); return; }
	if (AttachedPawn == Pawn && GetAttachParentActor() == Pawn) return;
	if (IsValid(AttachedPawn)) ISAFUnitInterface::Execute_DetachFromPawn(this);;
	

	// Attach and track tickless, set net policy & bind OnDestroy
	FAttachmentTransformRules Rules(EAttachmentRule::SnapToTarget, false);
	AttachToActor(Pawn, Rules);
	SetActorRelativeTransform(FTransform::Identity);
	if (AActor* PawnOwner = Pawn->GetOwner()) SetOwner(PawnOwner);
	AttachedPawn = Pawn;
	AttachedPawn->OnDestroyed.AddDynamic(this, &ASAFUnit::OnAttachedPawnDestroyedProxy);
	ForceNetUpdate();
}

// Detaches this Unit Actor from its pawn, if it is attached to one.
void ASAFUnit::DetachFromPawn_Implementation() {
	if (!HasAuthority()) return;

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (AttachedPawn) {
		AttachedPawn->OnDestroyed.RemoveDynamic(this, &ASAFUnit::OnAttachedPawnDestroyedProxy);
		AttachedPawn = nullptr;
	}

	ForceNetUpdate();
}

// Handles detachment on destruction of the pawn this Unit Actor is attached to.
void ASAFUnit::OnAttachedPawnDestroyed_Implementation(AActor* DestroyedPawn) {
	ISAFUnitInterface::Execute_DetachFromPawn(this);
}

// Proxy
void ASAFUnit::OnAttachedPawnDestroyedProxy(AActor* DestroyedPawn) {
	ISAFUnitInterface::Execute_OnAttachedPawnDestroyed(this, DestroyedPawn);
}

float ASAFUnit::GetFormationSpacing_Implementation() const { 
	USAFAsset* ResolvedAsset = SAFAssetResolver::ResolveAsset(Asset);
	return SAFSpacingResolver::ResolveSpacing(nullptr, ResolvedAsset); 
} 

// Call to issue this an order.
bool ASAFUnit::Order_Implementation(FSAFOrder Order) {
	const bool bTagValid = Order.Tag.IsValid();
	const FString TagStr  = bTagValid ? Order.Tag.ToString() : TEXT("<Invalid>");
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Order: '%s' failed: ASC is nullptr.", *GetName()));               return false; }
	if (!bTagValid)     { SAFDEBUG_ERROR(FORMATSTR("Order: '%s' failed: invalid tag '%s'.", *GetName(), *TagStr));    return false; }
	if (!bOrderable)    { SAFDEBUG_INFO (FORMATSTR("Order: '%s' rejected orders, it is not orderable.", *GetName())); return false; }

	// Make order visible to abilities and notify listeners
	CurrentOrder = Order;
	OnOrderReceived.Broadcast(Order);

	// Build gameplay event payload
	FGameplayEventData Data;
	Data.EventTag = Order.Tag;
	Data.Instigator = this;
	Data.Target = Order.Target.Get();
	Data.EventMagnitude = 1.f;
	Data.TargetData = SAFLibrary::MakeTargetDataFromOrder(Order, this);
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, Order.Tag, Data);

	SAFDEBUG_SUCCESS(FORMATSTR( //174
		"Unit '%s' dispatched GameplayEvent tagged '%s' at Actor '%s' with Start (%s) and End (%s).", 
		*GetName(), *TagStr, 
		Order.Target.Get() ? *Order.Target->GetName() : TEXT("<None>"), 
		*Order.Start.ToString(), 
		*Order.End.ToString()
	));
	
	return true;
}

// Call to retrieve all order tags this unit has assigned to it.
// (i.e. what orders this unit can execute).
void ASAFUnit::GetOrderTags_Implementation(TArray<FGameplayTag>& OutTags) const {
	if (!AbilitySystem) { SAFDEBUG_ERROR("GetOrderTags aborted: AbilitySystem was nullptr."); return; }
	
	OutTags.Reset();
	for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities()) {
		const UGameplayAbility* Ability = Spec.Ability;
		if (!Ability) { SAFDEBUG_WARNING("GetOrderTags skipped a null ability in one of the AbilitySpecs."); continue; }

		const FGameplayTagContainer& AssetTags = Ability->GetAssetTags();
		for (const FGameplayTag& Tag : AssetTags) OutTags.AddUnique(Tag);
	}
}

// Notifies listeners that the current order has been completed
bool ASAFUnit::NotifyOrderCompleted_Implementation() {
	OnOrderCompleted.Broadcast(this, CurrentOrder);
	SAFDEBUG_SUCCESS(FORMATSTR("'%s' completed order '%s'.", *GetName(), *this->CurrentOrder.Tag.ToString()));
	CurrentOrder = FSAFOrder{};
	return true;
}

// GAS Helpers
// ============================================================================================================================================================
void ASAFUnit::InitAbilitySystem() {
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' called InitASC but no AbilitySystemComponent was found.", *GetName()));      return; }
	AbilitySystem->InitAbilityActorInfo(this, this);
}

// Gives abilities on begin play based on the assigned asset
void ASAFUnit::GiveAbilitiesFromAsset() {
	if (!Asset)         { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given abilities: Asset was nullptr.", *GetName()));             return; }
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given abilities: AbilitySystem was nullptr.", *GetName()));     return; }

	for (const auto& Ability : GetUnitAsset()->Abilities) {
		if (!Ability.AbilityClass) { SAFDEBUG_WARNING("Ability skipped: An ability in the UnitData was null or invalid."); continue; }
		FGameplayAbilitySpec Spec(Ability.AbilityClass, Ability.Level, Ability.InputID);
		Spec.GetDynamicSpecSourceTags().AppendTags(Ability.AbilityTags);
		AbilitySystem->GiveAbility(Spec);
	}
}

// Gives attributes from the USAFUnitAttributes set on begin play, based on the assigned asset
void ASAFUnit::GiveAttributesFromAsset() {
	const USAFUnitAsset* UnitAsset = GetUnitAsset();
	if (!UnitAsset)     { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given attributes: UnitAsset was nullptr.", *GetName()));     return; }
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given attributes: AbilitySystem was nullptr.", *GetName())); return; }

	// 1) Ensure all AttributeSets listed on the asset are present on the ASC (no duplicates).
	TArray<UAttributeSet*> SetInstances;
	SetInstances.Reserve(UnitAsset->AttributeSets.Num());

	for (const TSubclassOf<UAttributeSet>& SetClass : UnitAsset->AttributeSets) {
		if (!*SetClass) continue;

		const UAttributeSet* ExistingConst = AbilitySystem->GetAttributeSet(SetClass);
		UAttributeSet* Instance = const_cast<UAttributeSet*>(ExistingConst);
		if (!Instance) {
			Instance = NewObject<UAttributeSet>(this, SetClass);
			if (ensure(Instance)) AbilitySystem->AddAttributeSetSubobject(Instance);
			else { SAFDEBUG_WARNING(FORMATSTR("Actor '%s' failed to create AttributeSet '%s'.", *GetName(), *SetClass->GetName())); continue; }
		}

		SetInstances.Add(Instance);
	}

	// 2) Seed from attribute table rows (if provided). Later rows win. Only for sets deriving from USAFAttributeSet.
	if (UnitAsset->AttributeTableRows.Num() == 0) 
		{ SAFDEBUG_INFO(FORMATSTR("Actor '%s' has no AttributeTableRows; using AttributeSet class defaults.", *GetName())); return; }
		
	for (UAttributeSet* Set : SetInstances) {
		if (!Set) continue;

		if (USAFAttributeSet* AttrBase = Cast<USAFAttributeSet>(Set)) {
			for (const FDataTableRowHandle& Handle : UnitAsset->AttributeTableRows) {
				if (Handle.DataTable && !Handle.RowName.IsNone())
					AttrBase->SeedFromRowHandle(Handle);
			}
		}
	}
}

// Gives tags on begin play bassed on the assigned asset
void ASAFUnit::GiveTagsFromAsset() {
	if (!Asset)         { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given tags: Asset was nullptr.", *GetName()));                  return; }
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not be given tags: AbilitySystem was nullptr.", *GetName()));          return; }

	const FGameplayTagContainer& InTags = GetUnitAsset()->Tags;
	if (InTags.IsEmpty()) return;

	int32 Granted = 0;
	for (const FGameplayTag& Tag : InTags) {
		if (!Tag.IsValid()) { SAFDEBUG_WARNING("GiveTagsFromAsset skipped an invalid tag in DefaultTags."); continue; }
		if (!AbilitySystem->HasMatchingGameplayTag(Tag)) {
			AbilitySystem->AddLooseGameplayTag(Tag);
			++Granted;
		}
	}

	if (Granted > 0) SAFDEBUG_SUCCESS(FORMATSTR("Actor '%s' granted %d default tag(s) from asset.", *GetName(), Granted));
}

// Applies GAS startup effects
void ASAFUnit::ApplyStartupEffects() {
	if (!Asset)         { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not apply startup effects: Asset was nullptr.", *GetName()));               return; }
	if (!AbilitySystem) { SAFDEBUG_ERROR(FORMATSTR("Actor '%s' could not apply startup effects: AbilitySystem was nullptr.", *GetName()));  return; }
	FGameplayEffectContextHandle Context = AbilitySystem->MakeEffectContext();
	Context.AddSourceObject(this);

	for (const auto& GameplayEffectClass : GetUnitAsset()->StartupEffects) {
		if (GameplayEffectClass) AbilitySystem->ApplyGameplayEffectToSelf(GameplayEffectClass->GetDefaultObject<UGameplayEffect>(), 1.f, Context);
		else SAFDEBUG_WARNING("GameplayEffectClass skipped during startup effects: GameplayEffectClass was nulltpr.");
	}
}

// Replication
// ==================================================================================================
void ASAFUnit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFUnit, AttachedPawn);
	DOREPLIFETIME(ASAFUnit, CurrentFormation);
	DOREPLIFETIME(ASAFUnit, ProductionComponent);
}

void ASAFUnit::OnRep_AttachedPawn() {
	SAFDEBUG_INFO(TEXT("OnRep_AttachedPawn triggered."));
}

void ASAFUnit::OnRep_CurrentFormation() {
	SAFDEBUG_INFO(TEXT("OnRep_CurrentFormation triggered."));
}
