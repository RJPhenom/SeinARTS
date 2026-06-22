// Native gameplay tags for the SeinARTS Framework.
//
// All tags consumed by framework C++ should be declared here and defined in
// SeinARTSGameplayTags.cpp. This is the single source of truth for
// framework-shipped tags — add new ones here as systems require them.
//
// Designer/game-specific tags (factions, unit identities, tech trees, terrain
// types, effects, etc.) should live in the project's own tag sources, not
// here. Only tags that the framework itself references belong in this file.
//
// All framework tags live under the "SeinARTS." root to keep them namespaced
// away from project tags.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"

namespace SeinARTSTags
{
	// --- Root ---
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SeinARTS);

	// --- Command ---
	// Root namespace for command-stream tags. Three sub-hierarchies:
	//   Command.Context.* — player click-intent descriptors (BuildCommandContext)
	//   Command.Type.*    — FSeinCommand command-type identifiers
	//   Command.Reject.*  — rejection-reason vocabulary attached to CommandRejected
	//                       visual events (DESIGN §13 path-reject plumbing; extended
	//                       during nav refactor Session 3.2).
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command);

	// --- Command.Context ---
	// Consumed by ASeinPlayerController::BuildCommandContext to describe the
	// player's click intent. Matched against FSeinAbilityComponent::DefaultCommands
	// (DESIGN §7 Q9) to resolve which ability tag to activate.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_RightClick);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_Target_Ground);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_Target_Friendly);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_Target_Neutral);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_Target_Enemy);

	// AbilityTriggered: stamped by USeinTargeterBPFL::IssueTargetedAbility when an
	// ability is invoked from the action-slot/targeter flow rather than right-click
	// smart-resolution. Tells the broker resolver "the ability is predetermined,
	// do not run DefaultCommands resolution against this context — dispatch the
	// already-resolved tag in the BrokerOrderPayload's CommandContext (the
	// ability's own AbilityTag, also placed into the context by the helper)."
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Context_AbilityTriggered);

	// --- Resource ---
	// Root for player-economy resource identifiers. Individual resource tags
	// (SeinARTS.Resource.Manpower, .Fuel, etc.) are designer-defined in project
	// tag sources or catalog entries — the framework only ships the root so
	// designer assets can filter pickers to SeinARTS.Resource.* tags.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource);

	// --- State ---
	// Root for transient entity-state tags the framework grants/ungrants on
	// entities to gate ability activation. Designers use these in
	// USeinAbility::BlockedTags to block actions while the entity is in a
	// given state (e.g. UnderConstruction blocks the building's production
	// abilities until construction completes).
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State);

	// Granted on entities with FSeinConstructionComponent while their construction
	// is active (Progress < BuildTime). Ungranted by USeinConstructionBPFL::
	// SeinFinishConstruction when the building completes. Buildings that
	// should refuse to act mid-construction add this tag to every relevant
	// ability's BlockedTags.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_UnderConstruction);

	// --- Command.Type ---
	// Every FSeinCommand carries a CommandType gameplay tag. Sim-side commands
	// live directly under SeinARTS.Command.Type.*; observer commands (logged for
	// replay reconstruction but not processed by the sim) live under
	// SeinARTS.Command.Type.Observer.*. FSeinCommand::IsObserverCommand tests
	// for descendance from the Observer parent tag.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_ActivateAbility);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_CancelAbility);
	// Command_Type_QueueProduction removed: production unified into ActivateAbility.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_CancelProduction);
	// Command_Type_SetRallyPoint removed: rally authoring via SA_SetRallyPoint
	// abilities calling USeinProductionBPFL::SeinSetRallyPoint.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Ping);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_BrokerOrder);

	// Match flow commands (DESIGN §18). State-machine transitions that don't
	// target a specific entity — `FSeinCommand::EntityHandle` is unused.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_StartMatch);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_PauseMatchRequest);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_ResumeMatchRequest);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_EndMatch);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_ConcedeMatch);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_RestartMatch);

	// Vote command types (Session 5.4).
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_StartVote);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_CastVote);

	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_CameraUpdate);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_SelectionChanged);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_ChatMessage);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_Emote);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_ControlGroupAssigned);

	// Selection observer commands (DESIGN §15). Logged for replay reconstruction —
	// the sim never processes these; they rebuild client UI state on playback.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_Selection);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_Selection_Replaced);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_Selection_Added);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_Selection_Removed);

	// Control-group observer commands (DESIGN §15).
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_ControlGroup);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_ControlGroup_Assigned);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_ControlGroup_AddedTo);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Type_Observer_ControlGroup_Selected);

	// --- Command.Reject ---
	// Reason-tag vocabulary attached to CommandRejected visual events for the
	// path-reject pathway. Designers extend with game-specific reasons
	// by adding their own tags under the parent.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_Unaffordable);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_OnCooldown);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_BlockedByTag);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_OutOfRange);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_InvalidTarget);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_NoLineOfSight);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_PathUnreachable);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_GoalUnwalkable);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_FootprintBlocked);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_MissingComponent);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_QueueFull);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_CanActivateFailed);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_SimPaused);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_SpectatorForbidden);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Command_Reject_MatchStateInvalid);

	// --- Environment ---
	// Framework ships `Environment.Default` only; games extend the vocabulary
	// (Environment.Grass, Environment.Snow, etc.) and interpret via effects (DESIGN §13).
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Environment_Default);

	// --- Formation ---
	// Formation-identity vocabulary. The framework ships Formation.Box (the default
	// right-click-drag formation) plus Wedge/Ring/Square options; an order gesture
	// nominates one and the command broker resolver maps it to a USeinFormation via
	// FormationsByTag. Games add their own formation tags under the Formation root.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Formation);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Formation_Box);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Formation_Wedge);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Formation_Ring);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Formation_Square);
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Formation_Blob);

	// --- UI ---
	// Render/UI-facing entity tags consumed by the UI Toolkit.
	// Minimap.Hidden: an entity carrying this tag is NOT drawn as a minimap blip
	// (smoke / vfx emitters, environmental props, capture markers, etc.). Authored via the
	// entity bridge's BaseTags. Default (no tag) = shown — so it never hides a unit by accident.
	SEINARTSCOREENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Minimap_Hidden);
}
