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

	// Match flow (DESIGN §18).
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_StartMatch,          "SeinARTS.Command.Type.StartMatch",          "Transition Lobby → Starting");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_PauseMatchRequest,   "SeinARTS.Command.Type.PauseMatchRequest",   "Request a sim pause (may be a vote trigger)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_ResumeMatchRequest,  "SeinARTS.Command.Type.ResumeMatchRequest",  "Request resume after pause");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_EndMatch,            "SeinARTS.Command.Type.EndMatch",            "Scenario / victory-code ends the match");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_ConcedeMatch,        "SeinARTS.Command.Type.ConcedeMatch",        "Player concedes (triggers EndMatch if victory condition met)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Command_Type_RestartMatch,        "SeinARTS.Command.Type.RestartMatch",        "Reset back to Lobby (requires vote or host authority)");

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

	// --- Environment ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Environment_Default, "SeinARTS.Environment.Default", "Default terrain environment tag (designers extend the namespace with biome/surface tags).");

	// --- Formation ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation,      "SeinARTS.Formation",      "Root for formation-identity tags (order gestures nominate one; the broker resolver maps to a USeinFormation)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Box,  "SeinARTS.Formation.Box",  "Default right-click-drag formation: a Total-War rank box (front width = the drag, depth fills to fit N)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Line,   "SeinARTS.Formation.Line",   "A true single-rank line spread along the drag (option; Box is the drag default)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Column, "SeinARTS.Formation.Column", "Single-file column (1 wide, N deep) trailing behind the lead");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Wedge,  "SeinARTS.Formation.Wedge",  "Wedge / arrowhead: tip forward, arms fanning back-left and back-right");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Ring,   "SeinARTS.Formation.Ring",   "Defensive ring: members spaced evenly around a circle about the anchor");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Formation_Blob,   "SeinARTS.Formation.Blob",   "Every member converges on the single order point (the classic single-destination move); the default gesture nominates this for a plain click when single-click formations are off");
}
