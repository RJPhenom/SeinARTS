/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVisualEvent.cpp
 * @brief   Implementation of visual event factory methods and event queue.
 */

#include "Events/SeinVisualEvent.h"

// -----------------------------------------------------------------------------
// FSeinVisualEvent factory methods
// -----------------------------------------------------------------------------

FSeinVisualEvent FSeinVisualEvent::MakeSpawnEvent(FSeinEntityHandle Entity, FFixedVector InLocation)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::EntitySpawned;
	Event.PrimaryEntity = Entity;
	Event.Location = InLocation;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeDestroyEvent(FSeinEntityHandle Entity)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::EntityDestroyed;
	Event.PrimaryEntity = Entity;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeDamageAppliedEvent(FSeinEntityHandle Target, FSeinEntityHandle Source, FFixedPoint Amount, FGameplayTag DamageType)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::DamageApplied;
	Event.PrimaryEntity = Target;
	Event.SecondaryEntity = Source;
	Event.Value = Amount;
	Event.Tag = DamageType;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeHealAppliedEvent(FSeinEntityHandle Target, FSeinEntityHandle Source, FFixedPoint Amount, FGameplayTag HealType)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::HealApplied;
	Event.PrimaryEntity = Target;
	Event.SecondaryEntity = Source;
	Event.Value = Amount;
	Event.Tag = HealType;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeDeathEvent(FSeinEntityHandle Dying, FSeinEntityHandle Killer)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::Death;
	Event.PrimaryEntity = Dying;
	Event.SecondaryEntity = Killer;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeKillEvent(FSeinEntityHandle Killer, FSeinEntityHandle Killed, FSeinPlayerID KillerPlayer)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::Kill;
	Event.PrimaryEntity = Killer;
	Event.SecondaryEntity = Killed;
	Event.PlayerID = KillerPlayer;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeAbilityEvent(FSeinEntityHandle Entity, FGameplayTag AbilityTag, bool bActivated)
{
	FSeinVisualEvent Event;
	Event.Type = bActivated ? ESeinVisualEventType::AbilityActivated : ESeinVisualEventType::AbilityEnded;
	Event.PrimaryEntity = Entity;
	Event.Tag = AbilityTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeProductionEvent(FSeinEntityHandle Building, FGameplayTag IdentityTag, bool bCompleted)
{
	FSeinVisualEvent Event;
	Event.Type = bCompleted ? ESeinVisualEventType::ProductionCompleted : ESeinVisualEventType::ProductionStarted;
	Event.PrimaryEntity = Building;
	Event.Tag = IdentityTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeEffectEvent(FSeinEntityHandle Target, FGameplayTag EffectTag, bool bApplied)
{
	FSeinVisualEvent Event;
	Event.Type = bApplied ? ESeinVisualEventType::EffectApplied : ESeinVisualEventType::EffectRemoved;
	Event.PrimaryEntity = Target;
	Event.Tag = EffectTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeSquadMemberAddedEvent(FSeinEntityHandle Squad, FSeinEntityHandle Member, FGameplayTag SlotTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::SquadMemberAdded;
	Event.PrimaryEntity = Squad;
	Event.SecondaryEntity = Member;
	Event.Tag = SlotTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeSquadMemberDiedEvent(FSeinEntityHandle Squad, FSeinEntityHandle Member, FGameplayTag SlotTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::SquadMemberDied;
	Event.PrimaryEntity = Squad;
	Event.SecondaryEntity = Member;
	Event.Tag = SlotTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeSquadLeaderChangedEvent(FSeinEntityHandle Squad, FSeinEntityHandle NewLeader)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::SquadLeaderChanged;
	Event.PrimaryEntity = Squad;
	Event.SecondaryEntity = NewLeader;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeTechResearchedEvent(FSeinPlayerID Player, FGameplayTag TechTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::TechResearched;
	Event.PlayerID = Player;
	Event.Tag = TechTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeTechRevokedEvent(FSeinPlayerID Player, FGameplayTag TechTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::TechRevoked;
	Event.PlayerID = Player;
	Event.Tag = TechTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeProductionStalledEvent(FSeinEntityHandle Building, FGameplayTag IdentityTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::ProductionStalled;
	Event.PrimaryEntity = Building;
	Event.Tag = IdentityTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeConstructionStateChangedEvent(FSeinEntityHandle Entity, bool bUnderConstruction)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::ConstructionStateChanged;
	Event.PrimaryEntity = Entity;
	// Encode state in Value: 1.0 = entering construction, 0.0 = leaving.
	Event.Value = bUnderConstruction ? FFixedPoint::One : FFixedPoint::Zero;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakePingEvent(FSeinPlayerID Player, FFixedVector InLocation, FSeinEntityHandle OptionalTarget)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::Ping;
	Event.PlayerID = Player;
	Event.Location = InLocation;
	Event.PrimaryEntity = OptionalTarget;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeCommandRejectedEvent(FSeinPlayerID Player, FSeinEntityHandle Entity, FGameplayTag CommandTag, FGameplayTag ReasonTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::CommandRejected;
	Event.PlayerID = Player;
	Event.PrimaryEntity = Entity;
	Event.Tag = CommandTag;
	Event.ReasonTag = ReasonTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeNavLinkChangedEvent(int32 NavLinkID, bool bEnabled)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::NavLinkChanged;
	Event.Value = FFixedPoint::FromInt(NavLinkID);
	// Encode enabled flag in Location.X as 0 or 1 (dedicated bool field avoided to
	// keep FSeinVisualEvent compact; render consumers check Location.X > 0).
	Event.Location.X = bEnabled ? FFixedPoint::One : FFixedPoint::Zero;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeTerrainMutatedEvent(FFixedVector InLocation, int32 CellCount)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::TerrainMutated;
	Event.Location = InLocation;
	Event.Value = FFixedPoint::FromInt(CellCount);
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeEntityEnteredContainerEvent(FSeinEntityHandle Container, FSeinEntityHandle Occupant, int32 VisualSlotIndex)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::EntityEnteredContainer;
	Event.PrimaryEntity = Container;
	Event.SecondaryEntity = Occupant;
	Event.Value = FFixedPoint::FromInt(VisualSlotIndex);
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeEntityExitedContainerEvent(FSeinEntityHandle Container, FSeinEntityHandle Occupant, FFixedVector ExitLocation)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::EntityExitedContainer;
	Event.PrimaryEntity = Container;
	Event.SecondaryEntity = Occupant;
	Event.Location = ExitLocation;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeAttachmentSlotFilledEvent(FSeinEntityHandle Container, FSeinEntityHandle Occupant, FGameplayTag SlotTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::AttachmentSlotFilled;
	Event.PrimaryEntity = Container;
	Event.SecondaryEntity = Occupant;
	Event.Tag = SlotTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeAttachmentSlotEmptiedEvent(FSeinEntityHandle Container, FSeinEntityHandle Occupant, FGameplayTag SlotTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::AttachmentSlotEmptied;
	Event.PrimaryEntity = Container;
	Event.SecondaryEntity = Occupant;
	Event.Tag = SlotTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakePlayPreRenderedCinematicEvent(FGameplayTag CinematicIDTag, int32 SkipMode, int32 VoteThreshold)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::PlayPreRenderedCinematic;
	Event.Tag = CinematicIDTag;
	const int32 PackedValue = (SkipMode & 0xFF) | (VoteThreshold << 8);
	Event.Value = FFixedPoint::FromInt(PackedValue);
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeEndCinematicEvent(FGameplayTag CinematicIDTag)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::EndCinematic;
	Event.Tag = CinematicIDTag;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeMatchFlowEvent(ESeinVisualEventType Type, FSeinPlayerID Winner, FGameplayTag Reason)
{
	FSeinVisualEvent Event;
	Event.Type = Type;
	Event.PlayerID = Winner;
	Event.Tag = Reason;
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeVoteStartedEvent(FGameplayTag VoteType, FSeinPlayerID Initiator, int32 RequiredThreshold)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::VoteStarted;
	Event.Tag = VoteType;
	Event.PlayerID = Initiator;
	Event.Value = FFixedPoint::FromInt(RequiredThreshold);
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeVoteProgressEvent(FGameplayTag VoteType, int32 YesCount, int32 NoCount)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::VoteProgress;
	Event.Tag = VoteType;
	const int32 Packed = (YesCount & 0xFFFF) | ((NoCount & 0xFFFF) << 16);
	Event.Value = FFixedPoint::FromInt(Packed);
	return Event;
}

FSeinVisualEvent FSeinVisualEvent::MakeVoteResolvedEvent(FGameplayTag VoteType, bool bPassed)
{
	FSeinVisualEvent Event;
	Event.Type = ESeinVisualEventType::VoteResolved;
	Event.Tag = VoteType;
	Event.Value = FFixedPoint::FromInt(bPassed ? 1 : 0);
	return Event;
}

// -----------------------------------------------------------------------------
// FSeinVisualEventQueue
// -----------------------------------------------------------------------------

void FSeinVisualEventQueue::Enqueue(const FSeinVisualEvent& Event)
{
	Events.Add(Event);
}

TArray<FSeinVisualEvent> FSeinVisualEventQueue::Flush()
{
	TArray<FSeinVisualEvent> Flushed = MoveTemp(Events);
	Events.Reset();
	return Flushed;
}

int32 FSeinVisualEventQueue::Num() const
{
	return Events.Num();
}
