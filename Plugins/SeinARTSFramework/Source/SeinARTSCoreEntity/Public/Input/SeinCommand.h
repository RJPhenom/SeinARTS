/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommand.h
 * @brief   Deterministic command system. Commands are issued by players/AI
 *          and processed during the CommandProcessing tick phase. CommandType
 *          plus SchemaVersion form the exact registered wire-schema key.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "Abilities/SeinTargeterTypes.h"
#include "SeinCommand.generated.h"

/** Limits that are part of the cross-module command protocol contract. */
namespace SeinCommandProtocolLimits
{
	constexpr int32 MaxCommandsPerAuthor = 1024;
}

/**
 * Authenticated authority carried with a canonical command.
 *
 * Callers may construct Unauthenticated commands, but a trusted ingress must
 * replace that value before simulation. Network transports derive it from the
 * authenticated participant binding; deterministic systems stamp their own
 * follow-up commands locally on every peer. Coordinator status is deliberately
 * absent: gathering a turn never grants gameplay or match-control authority.
 */
UENUM(BlueprintType)
enum class ESeinCommandIssuerKind : uint8
{
	Unauthenticated,
	Player,
	MatchAdministrator,
	DeterministicSystem,
};

/**
 * A single deterministic command from a player or AI to an entity.
 * Fully value-typed and serializable for lockstep networking.
 *
 * CommandType is a gameplay tag. Framework-shipped types live under
 * SeinARTS.Command.Type.* (see SeinARTSGameplayTags.h); observer-only
 * types (logged for replay reconstruction but not processed by the sim)
 * live under SeinARTS.Command.Type.Observer.*.
 *
 * Commands carry a fixed set of common fields plus an optional
 * FInstancedStruct Payload for type-specific data (shift-queue info,
 * broker orders, etc.). Simple commands leave Payload empty.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCommand
{
	GENERATED_BODY()

	/** Player who issued the command */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FSeinPlayerID PlayerID;

	/** Trusted ingress classification. Never trust a value supplied over the wire. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	ESeinCommandIssuerKind IssuerKind = ESeinCommandIssuerKind::Unauthenticated;

	/** Snapshotted payer for a deterministic-system ActivateAbility follow-up. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Authority")
	FSeinPlayerID DerivedResourcePayer;

	/** Entity this command targets */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FSeinEntityHandle EntityHandle;

	/** What kind of command this is — a tag under SeinARTS.Command.Type.*  */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FGameplayTag CommandType;

	/** Positive wire schema version. Exact matching only; no nearest-version fallback. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	int32 SchemaVersion = 1;

	/** Gameplay tag identifying the ability or entity identity */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FGameplayTag AbilityTag;

	/** Optional target entity (e.g., attack target) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FSeinEntityHandle TargetEntity;

	/** Optional target location (e.g., move destination, rally point) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FFixedVector TargetLocation;

	/** Sim tick this command is intended for */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	int32 Tick = 0;

	/** Index into the production queue (for CancelProduction) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	int32 QueueIndex = -1;

	/** Whether to queue (append) rather than replace the current ability */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	bool bQueueCommand = false;

	/** Auxiliary world location (e.g., formation endpoint for drag orders) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FFixedVector AuxLocation;

	/** Captured targeter points for ActivateAbility commands originating from
	 *  the targeter UI flow (multi-cast smoke grenades, line corridors, etc.).
	 *  Empty for typical right-click commands. When non-empty, ProcessCommands'
	 *  ActivateAbility branch dispatches via ActivateAbilityWithTargeterPoints
	 *  so the ability's runtime TargeterPoints array is populated. Carried
	 *  through broker per-member dispatches so the targeter intent survives
	 *  the multi-unit fan-out. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	TArray<FSeinTargeterPoint> TargeterPoints;

	// --- Observer data (used by CameraUpdate / SelectionChanged, ignored by sim) ---

	/** Auxiliary fixed-point value A (camera yaw for CameraUpdate) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Observer")
	FFixedPoint AuxA;

	/** Auxiliary fixed-point value B (camera zoom distance for CameraUpdate) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Observer")
	FFixedPoint AuxB;

	/** Entity list (selected entities for SelectionChanged) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Observer")
	TArray<FSeinEntityHandle> EntityList;

	/** Active focus index within selection (-1 = all, 0+ = index into EntityList) */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command|Observer")
	int32 ActiveFocusIndex = -1;

	/** Optional typed payload for command types that need more than the common fields.
	 *  Payload types are named USTRUCTs (FSeinShiftQueuePayload, FSeinBrokerOrderPayload, ...).
	 *  Simple commands leave Payload empty. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Command")
	FInstancedStruct Payload;

	// --- Helpers ---

	/** Returns true iff CommandType is a descendant of SeinARTS.Command.Type.Observer
	 *  (logged for replay but skipped by the sim tick). */
	bool IsObserverCommand() const;

	// --- Factory methods ---

	/** Create an ability activation command, optionally with a target entity and location. */
	static FSeinCommand MakeAbilityCommand(
		FSeinPlayerID Player,
		FSeinEntityHandle Entity,
		FGameplayTag Tag,
		FSeinEntityHandle Target = FSeinEntityHandle::Invalid(),
		FFixedVector Location = FFixedVector());

	/** Create a cancel-ability command. */
	static FSeinCommand MakeCancelCommand(
		FSeinPlayerID Player,
		FSeinEntityHandle Entity);

	/** Create a cancel-production command targeting a specific queue index on
	 *  a producer entity. Refunds the AtEnqueue cost per the producible
	 *  archetype's RefundPolicy (progress-proportional by default). */
	static FSeinCommand MakeCancelProductionCommand(
		FSeinPlayerID Player,
		FSeinEntityHandle Producer,
		int32 QueueIndex);

	// MakeProductionCommand + MakeRallyPointCommand removed (refactored 2026-05-05):
	// production now flows through MakeAbilityCommand on production-marked
	// abilities; rally authoring goes through SA_SetRallyPoint abilities.

	/**
	 * Legacy source-compatible ability factory. Queue/formation arguments are
	 * ignored because those semantics belong to BrokerOrder payloads.
	 */
	static FSeinCommand MakeAbilityCommandEx(
		FSeinPlayerID Player,
		FSeinEntityHandle Entity,
		FGameplayTag Tag,
		FSeinEntityHandle Target,
		FFixedVector Location,
		bool bQueue,
		FFixedVector FormationEnd = FFixedVector());

	/** Create a ping command (visible to all players). */
	static FSeinCommand MakePingCommand(
		FSeinPlayerID Player,
		FFixedVector Location,
		FSeinEntityHandle OptionalTarget = FSeinEntityHandle::Invalid());

	// --- Observer command factories (for replay reconstruction) ---

	/** Create a camera snapshot command. */
	static FSeinCommand MakeCameraUpdateCommand(
		FSeinPlayerID Player,
		FFixedVector PivotLocation,
		FFixedPoint Yaw,
		FFixedPoint ZoomDistance);

	/** Create a selection changed command. */
	static FSeinCommand MakeSelectionChangedCommand(
		FSeinPlayerID Player,
		const TArray<FSeinEntityHandle>& SelectedEntities,
		int32 InActiveFocusIndex = -1);
};

/**
 * Collects commands for a single sim tick. Consumed and cleared each frame
 * during the CommandProcessing phase.
 */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinCommandBuffer
{
	GENERATED_BODY()

	/** Buffered commands for this tick */
	UPROPERTY()
	TArray<FSeinCommand> Commands;

	/** Enqueue a command. */
	void AddCommand(const FSeinCommand& Cmd);

	/** Remove all buffered commands. */
	void Clear();

	/** Number of buffered commands. */
	int32 Num() const;

	/** Read-only access to the command array. */
	const TArray<FSeinCommand>& GetCommands() const;

	/** Move all buffered commands out, leaving an empty reusable buffer. */
	TArray<FSeinCommand> DrainCommands();
};
