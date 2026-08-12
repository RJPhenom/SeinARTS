#include "Tags/SeinARTSGameplayTags.h"

namespace SeinARTSTags
{
	// --- Root ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SeinARTS, "SeinARTS", "Root tag for all SeinARTS Framework tags");

	// --- Command ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command, "SeinARTS.Command", "Root for command-stream tags (Type / Context / Reject sub-hierarchies)");

	// --- Command.Context ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context,                 "SeinARTS.Command.Context",                 "Root tag for player click-intent descriptors consumed by BuildCommandContext");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_RightClick,      "SeinARTS.Command.Context.RightClick",      "Right mouse button smart command");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_Target_Ground,   "SeinARTS.Command.Context.Target.Ground",   "Click target is empty ground");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_Target_Friendly, "SeinARTS.Command.Context.Target.Friendly", "Click target is a friendly entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_Target_Neutral,  "SeinARTS.Command.Context.Target.Neutral",  "Click target is a neutral entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_Target_Enemy,    "SeinARTS.Command.Context.Target.Enemy",    "Click target is an enemy entity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Context_AbilityTriggered, "SeinARTS.Command.Context.AbilityTriggered", "Targeter-originated activation: ability tag is predetermined, skip DefaultCommands resolution");

	// --- Resource ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Resource, "SeinARTS.Resource", "Root tag for designer-defined economy resource identifiers");

	// --- State ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State,                   "SeinARTS.State",                   "Root for transient entity-state tags (UnderConstruction, etc.)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_UnderConstruction, "SeinARTS.State.UnderConstruction", "Entity has active FSeinConstructionComponent; gates production / combat abilities mid-build");

	// --- Command.Type ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type,                   "SeinARTS.Command.Type",                   "Root tag for FSeinCommand command types");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_ActivateAbility,   "SeinARTS.Command.Type.ActivateAbility",   "Activate an ability by tag");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_CancelAbility,     "SeinARTS.Command.Type.CancelAbility",     "Cancel the currently active ability");
	// Command_Type_QueueProduction removed: production unified into ActivateAbility.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_CancelProduction,  "SeinARTS.Command.Type.CancelProduction",  "Cancel a specific item in the production queue");
	// Command_Type_SetRallyPoint removed: rally via SA_SetRallyPoint abilities + SeinSetRallyPoint BPFL.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Ping,              "SeinARTS.Command.Type.Ping",              "Ping a location (visible to all players)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_BrokerOrder,       "SeinARTS.Command.Type.BrokerOrder",       "Multi-unit dispatch routed through a CommandBroker (DESIGN §5)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_SetPairCapability, "SeinARTS.Command.Type.SetPairCapability", "Match-control mutation of one directional player-pair capability grant");

	// Match flow (DESIGN §18).
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_PauseMatchRequest,   "SeinARTS.Command.Type.PauseMatchRequest",   "Request a sim pause (may be a vote trigger)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_ResumeMatchRequest,  "SeinARTS.Command.Type.ResumeMatchRequest",  "Request resume after pause");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_EndMatch,            "SeinARTS.Command.Type.EndMatch",            "Scenario / victory-code ends the match");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_ConcedeMatch,        "SeinARTS.Command.Type.ConcedeMatch",        "Player concedes (triggers EndMatch if victory condition met)");

	// Votes.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_StartVote,           "SeinARTS.Command.Type.StartVote",           "Initiate a vote");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_CastVote,            "SeinARTS.Command.Type.CastVote",            "Cast a yes/no vote");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer,                        "SeinARTS.Command.Type.Observer",                        "Parent for observer-only commands (logged for replay, skipped by sim)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_CameraUpdate,           "SeinARTS.Command.Type.Observer.CameraUpdate",           "Periodic camera position snapshot");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_SelectionChanged,       "SeinARTS.Command.Type.Observer.SelectionChanged",       "Player changed selection and/or active focus");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_ChatMessage,            "SeinARTS.Command.Type.Observer.ChatMessage",            "Player sent a chat message");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_Emote,                  "SeinARTS.Command.Type.Observer.Emote",                  "Player triggered a quick-chat emote");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_ControlGroupAssigned,   "SeinARTS.Command.Type.Observer.ControlGroupAssigned",   "Player assigned a control group (legacy monolithic tag — superseded by Selection.* / ControlGroup.* subtree)");

	// Selection observer subtree (DESIGN §15).
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_Selection,          "SeinARTS.Command.Type.Observer.Selection",          "Parent for selection observer commands");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_Selection_Replaced, "SeinARTS.Command.Type.Observer.Selection.Replaced", "Full replacement (non-shift click / box-select)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_Selection_Added,    "SeinARTS.Command.Type.Observer.Selection.Added",    "Shift-click / shift-box additive selection");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_Selection_Removed,  "SeinARTS.Command.Type.Observer.Selection.Removed",  "Ctrl-click / ctrl-box subtractive selection");

	// ControlGroup observer subtree.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_ControlGroup,           "SeinARTS.Command.Type.Observer.ControlGroup",           "Parent for control-group observer commands");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_ControlGroup_Assigned,  "SeinARTS.Command.Type.Observer.ControlGroup.Assigned",  "Bind control group to the current selection");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_ControlGroup_AddedTo,   "SeinARTS.Command.Type.Observer.ControlGroup.AddedTo",   "Shift-bind adds to existing control group");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_Observer_ControlGroup_Selected,  "SeinARTS.Command.Type.Observer.ControlGroup.Selected",  "Hotkey recalled a control group");

	// --- Command.Reject ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject,                    "SeinARTS.Command.Reject",                    "Root for CommandRejected reason-tag vocabulary");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_Unaffordable,       "SeinARTS.Command.Reject.Unaffordable",       "SeinCanAfford returned false");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_OnCooldown,         "SeinARTS.Command.Reject.OnCooldown",         "Ability is on cooldown");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_BlockedByTag,       "SeinARTS.Command.Reject.BlockedByTag",       "Entity carries a tag in BlockedTags, or missing RequiredEntityTags / RequiredPlayerTags");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_OutOfRange,         "SeinARTS.Command.Reject.OutOfRange",         "Target is outside the ability's MaxRange");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_InvalidTarget,      "SeinARTS.Command.Reject.InvalidTarget",      "Target fails ValidTargetTags");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_NoLineOfSight,      "SeinARTS.Command.Reject.NoLineOfSight",      "Target is not in line of sight (§12)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_PathUnreachable,    "SeinARTS.Command.Reject.PathUnreachable",    "Nav graph has no abstract path from source to goal");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_GoalUnwalkable,     "SeinARTS.Command.Reject.GoalUnwalkable",     "Target cell is blocked / off-map for this agent");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_FootprintBlocked,   "SeinARTS.Command.Reject.FootprintBlocked",   "Building footprint overlaps blocked cells (placement gate)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_MissingComponent,   "SeinARTS.Command.Reject.MissingComponent",   "Entity lacks the component the command needs");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_QueueFull,          "SeinARTS.Command.Reject.QueueFull",          "Production queue is at capacity");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_CanActivateFailed,  "SeinARTS.Command.Reject.CanActivateFailed",  "USeinAbility::CanActivate BP escape returned false");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_SimPaused,          "SeinARTS.Command.Reject.SimPaused",          "Sim is paused in Hard mode; command rejected");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_SpectatorForbidden, "SeinARTS.Command.Reject.SpectatorForbidden", "Spectator tried to emit a sim-mutating command");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_MatchStateInvalid,  "SeinARTS.Command.Reject.MatchStateInvalid",  "Match is not in a state that accepts this command (e.g., commands during Starting countdown)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_Unauthorized,       "SeinARTS.Command.Reject.Unauthorized",       "Authenticated issuer lacks the command's registered authority scope");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_Malformed,          "SeinARTS.Command.Reject.Malformed",          "Command envelope or payload does not match its registered schema");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_UnsupportedSchema,  "SeinARTS.Command.Reject.UnsupportedSchema",  "Command tag or exact schema version is not registered");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Reject_PayloadTooLarge,    "SeinARTS.Command.Reject.PayloadTooLarge",    "Command exceeds its registered deterministic payload budget");

	// --- Environment ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Environment_Default, "SeinARTS.Environment.Default", "Default terrain environment tag (designers extend the namespace with biome/surface tags).");

	// --- Formation ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation,      "SeinARTS.Formation",      "Root for formation-identity tags (order gestures nominate one; the broker resolver maps to a USeinFormation)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Box,  "SeinARTS.Formation.Box",  "Default right-click-drag formation: a rank-and-file block (front width = the drag, depth fills to fit N)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Wedge,  "SeinARTS.Formation.Wedge",  "Wedge / arrowhead: hollow nested chevrons, biggest at the tip; large selections fan into concentric chevron layers");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Ring,   "SeinARTS.Formation.Ring",   "Defensive ring: members evenly around a circle about the anchor; large selections fan into concentric rings");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Square, "SeinARTS.Formation.Square", "A hollow rank-and-file square outline about the anchor; large selections fan into concentric nested squares");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Blob,   "SeinARTS.Formation.Blob",   "Every member converges on the single order point (the classic single-destination move); the default gesture nominates this for a plain click when single-click formations are off");

	// --- Relationship ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Relationship, "SeinARTS.Relationship", "Root for directional player-pair relationship capability tags");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Relationship_Capability, "SeinARTS.Relationship.Capability", "Root for precise directional player-pair capabilities");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Relationship_Capability_ShareVision, "SeinARTS.Relationship.Capability.ShareVision", "A -> B means B may consume A's vision");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Relationship_Capability_PresentAsFriendly, "SeinARTS.Relationship.Capability.PresentAsFriendly", "A -> B means B should present A-owned entities as friendly for compatibility UI");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Relationship_Source, "SeinARTS.Relationship.Source", "Root for source identities that contribute pair-capability grants");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Relationship_Source_TeamBootstrap, "SeinARTS.Relationship.Source.TeamBootstrap", "Initial same-team compatibility grants seeded from nonzero TeamID");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Relationship_Source_MatchAdministration, "SeinARTS.Relationship.Source.MatchAdministration", "Authorized direct match-control pair-capability grants");

	// --- UI ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(UI_Minimap_Hidden, "SeinARTS.UI.Minimap.Hidden", "Entity opts out of appearing as a minimap blip (smoke / vfx emitters, environmental props, etc.). Authored via the entity bridge's BaseTags; default (no tag) = shown.");
}
