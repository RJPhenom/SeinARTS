#include "SeinConsumerMovementQualification.h"

#include "Abilities/SeinMoveToProxy.h"
#include "Actor/SeinEntityComponent.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Data/SeinWheeledMovementData.h"
#include "Movement/SeinWheeledVehicleMovement.h"
#include "NativeGameplayTags.h"
#include "Serialization/SeinCanonicalInitialStateDigest.h"
#include "StructUtils/InstancedStruct.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(
		TAG_SeinConsumerQualificationMove,
		"SeinConsumer.Qualification.Move");
}

USeinConsumerQualificationMoveAbility::
	USeinConsumerQualificationMoveAbility()
{
	AbilityName = FText::FromString(TEXT("Consumer Qualification Move"));
	AbilityTag = TAG_SeinConsumerQualificationMove;
	bIsMoveAbility = true;
}

void USeinConsumerQualificationMoveAbility::OnActivate_Implementation()
{
	if (USeinMoveToProxy* Proxy = USeinMoveToProxy::SeinMoveTo(
		this, TargetLocation))
	{
		Proxy->Activate();
	}
}

bool USeinConsumerQualificationNavigation::ComputeStaticEnvironmentDigest(
	FGuid& OutDigest,
	FString& OutError) const
{
	FSeinCanonicalDigestWriter Writer(
		TEXT("SeinConsumer.Qualification.Navigation"), 1);
	Writer.WriteString(TEXT("AllOpenPlane"));
	return Writer.Finalize(OutDigest, OutError);
}

bool USeinConsumerQualificationNavigation::ComputeStateCoverageClaim(
	FSeinNavigationStateCoverageClaim& OutClaim,
	FString& OutError) const
{
	OutError.Reset();
	OutClaim = FSeinNavigationStateCoverageClaim();
	OutClaim.StableImplementationId =
		TEXT("SeinConsumer.Qualification.Navigation");
	OutClaim.BehaviorRevision = 1;
	OutClaim.CoverageRevision = 1;
	OutClaim.StateCoverage = ESeinNavigationStateCoverage::Stateless;
	return true;
}

bool USeinConsumerQualificationNavigation::FindPath(
	const FSeinPathRequest& Request,
	FSeinPath& OutPath) const
{
	OutPath.Clear();
	OutPath.Waypoints.Add(Request.End);
	OutPath.bIsValid = true;
	OutPath.bIsPartial = false;
	OutPath.DeriveSegmentsFromWaypoints();
	return true;
}

bool USeinConsumerQualificationNavigation::FindCellPath(
	const FSeinPathRequest& Request,
	FSeinPath& OutPath) const
{
	return FindPath(Request, OutPath);
}

bool USeinConsumerQualificationNavigation::IsPassable(
	const FFixedVector& WorldPos) const
{
	(void)WorldPos;
	return true;
}

bool USeinConsumerQualificationNavigation::IsWorldPositionClear(
	const FFixedVector& WorldPos,
	uint8 AgentNavLayerMask) const
{
	(void)WorldPos;
	(void)AgentNavLayerMask;
	return true;
}

bool USeinConsumerQualificationNavigation::GetCellHeightAt(
	const FFixedVector& WorldPos,
	FFixedPoint& OutZ,
	bool bWalkableOnly) const
{
	(void)WorldPos;
	(void)bWalkableOnly;
	OutZ = FFixedPoint::Zero;
	return true;
}

ASeinConsumerMovementUnit::ASeinConsumerMovementUnit()
{
	USeinEntityComponent* Bridge = GetEntityBridge();
	check(Bridge);
	Bridge->bIsAbstract = true;
	Bridge->ComponentData.Reset();

	FSeinMovementComponent Movement;
	Movement.MovementClass = FSoftClassPath(
		USeinWheeledVehicleMovement::StaticClass());
	// Keep the qualification order active across the bounded adverse-network
	// reconnect window so snapshot adoption must restore live movement state.
	Movement.TopSpeed = FFixedPoint::FromInt(50);
	Movement.TurnRate = FFixedPoint::FromInt(3)
		/ FFixedPoint::FromInt(2);
	Movement.ReverseTopSpeed = Movement.TopSpeed * FFixedPoint::Half;
	Movement.ReverseEngageDistanceThreshold = FFixedPoint::FromInt(600);
	Movement.AvoidanceStrength = FFixedPoint::Zero;
	Movement.MovementClassData = FInstancedStruct::Make(
		FSeinWheeledMovementData());
	Bridge->ComponentData.Add(FInstancedStruct::Make(Movement));

	FSeinNavigationComponent Navigation;
	Navigation.FallbackFootprintRadius = FFixedPoint::FromInt(85);
	Navigation.AcceptanceRadius = FFixedPoint::FromInt(80);
	Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
	Navigation.OffPathThreshold = FFixedPoint::FromInt(10000);
	Bridge->ComponentData.Add(FInstancedStruct::Make(Navigation));

	FSeinAbilityComponent Abilities;
	Abilities.GrantedAbilities.Add(
		USeinConsumerQualificationMoveAbility::StaticClass());
	Bridge->ComponentData.Add(FInstancedStruct::Make(Abilities));
}
