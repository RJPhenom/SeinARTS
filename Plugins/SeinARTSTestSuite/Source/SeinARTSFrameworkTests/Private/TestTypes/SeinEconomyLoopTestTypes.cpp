/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinEconomyLoopTestTypes.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements designer-style economy test abilities exclusively
 *               through public framework composition surfaces.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "TestTypes/SeinEconomyLoopTestTypes.h"

#include "Lib/SeinComponentBPFL.h"
#include "Lib/SeinConstructionBPFL.h"
#include "Lib/SeinResourceBPFL.h"
#include "Lib/SeinSimMutationBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

USeinEconomyGatherTestAbility::USeinEconomyGatherTestAbility()
{
	TargetType = ESeinAbilityTargetType::Entity;
}

void USeinEconomyGatherTestAbility::OnActivate_Implementation()
{
	FInstancedStruct NodeData;
	FInstancedStruct CargoData;
	const bool bReadNode = USeinComponentBPFL::SeinGetComponentData(
		WorldSubsystem,
		TargetEntity,
		FSeinEconomyResourceNodeTestComponent::StaticStruct(),
		NodeData);
	const bool bReadCargo = USeinComponentBPFL::SeinGetComponentData(
		WorldSubsystem,
		OwnerEntity,
		FSeinEconomyCargoTestComponent::StaticStruct(),
		CargoData);
	const FSeinEconomyResourceNodeTestComponent* Node =
		NodeData.GetPtr<FSeinEconomyResourceNodeTestComponent>();
	const FSeinEconomyCargoTestComponent* Cargo =
		CargoData.GetPtr<FSeinEconomyCargoTestComponent>();
	if (bReadNode && bReadCargo && Node && Cargo
		&& GatherAmount > FFixedPoint::Zero
		&& Node->Available > FFixedPoint::Zero
		&& Cargo->Capacity > Cargo->Amount)
	{
		const FFixedPoint Room = Cargo->Capacity - Cargo->Amount;
		FFixedPoint Transfer = GatherAmount;
		if (Transfer > Node->Available)
		{
			Transfer = Node->Available;
		}
		if (Transfer > Room)
		{
			Transfer = Room;
		}

		FSeinEconomyResourceNodeTestComponent NewNode = *Node;
		FSeinEconomyCargoTestComponent NewCargo = *Cargo;
		NewNode.Available -= Transfer;
		NewCargo.Amount += Transfer;
		USeinSimMutationBPFL::SeinSetComponent(
			WorldSubsystem,
			TargetEntity,
			FSeinEconomyResourceNodeTestComponent::StaticStruct(),
			FInstancedStruct::Make(NewNode));
		USeinSimMutationBPFL::SeinSetComponent(
			WorldSubsystem,
			OwnerEntity,
			FSeinEconomyCargoTestComponent::StaticStruct(),
			FInstancedStruct::Make(NewCargo));
	}
	EndAbility();
}

USeinEconomyDropoffTestAbility::USeinEconomyDropoffTestAbility()
{
	TargetType = ESeinAbilityTargetType::Entity;
}

void USeinEconomyDropoffTestAbility::OnActivate_Implementation()
{
	FInstancedStruct DropoffData;
	FInstancedStruct CargoData;
	const bool bReadDropoff = USeinComponentBPFL::SeinGetComponentData(
		WorldSubsystem,
		TargetEntity,
		FSeinEconomyDropoffTestComponent::StaticStruct(),
		DropoffData);
	const bool bReadCargo = USeinComponentBPFL::SeinGetComponentData(
		WorldSubsystem,
		OwnerEntity,
		FSeinEconomyCargoTestComponent::StaticStruct(),
		CargoData);
	const FSeinEconomyCargoTestComponent* Cargo =
		CargoData.GetPtr<FSeinEconomyCargoTestComponent>();
	if (bReadDropoff && bReadCargo && Cargo
		&& Cargo->Amount > FFixedPoint::Zero)
	{
		FSeinResourceCost Income;
		Income.Amounts.Add(SeinARTSTags::Resource, Cargo->Amount);
		USeinResourceBPFL::SeinGrantIncome(
			WorldSubsystem,
			WorldSubsystem->GetEntityOwner(OwnerEntity),
			Income);

		FSeinEconomyCargoTestComponent EmptyCargo = *Cargo;
		EmptyCargo.Amount = FFixedPoint::Zero;
		USeinSimMutationBPFL::SeinSetComponent(
			WorldSubsystem,
			OwnerEntity,
			FSeinEconomyCargoTestComponent::StaticStruct(),
			FInstancedStruct::Make(EmptyCargo));
	}
	EndAbility();
}

USeinEconomyConstructTestAbility::USeinEconomyConstructTestAbility()
{
	TargetType = ESeinAbilityTargetType::Entity;
}

void USeinEconomyConstructTestAbility::OnTick_Implementation(
	FFixedPoint DeltaTime)
{
	if (!WorldSubsystem || !TargetEntity.IsValid()
		|| BuildRate <= FFixedPoint::Zero)
	{
		CancelAbility();
		return;
	}

	if (USeinConstructionBPFL::SeinAddConstructionProgress(
			WorldSubsystem, TargetEntity, BuildRate * DeltaTime))
	{
		EndAbility();
	}
	else if (!USeinConstructionBPFL::SeinIsUnderConstruction(
		WorldSubsystem, TargetEntity))
	{
		CancelAbility();
	}
}
