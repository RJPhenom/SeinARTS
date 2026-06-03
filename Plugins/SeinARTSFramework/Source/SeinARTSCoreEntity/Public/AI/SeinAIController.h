/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAIController.h
 * @brief   Thin abstract UObject base class for strategic AI controllers
 *          (DESIGN §16). Sits ABOVE the sim layer — emits commands the same
 *          way a human player controller would.
 *
 *          Designers subclass this in Blueprint to implement BT / FSM /
 *          utility / GOAP / custom reasoning. Framework provides the tick
 *          hook + `EmitCommand` plumbing + sim-state query BPFLs; everything
 *          else is designer freestyle.
 *
 *          AI internal reasoning does NOT need to be deterministic — only
 *          one machine runs the AI in multiplayer, and only emitted commands
 *          cross the lockstep boundary.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "SeinAIController.generated.h"

class USeinWorldSubsystem;
struct FSeinCommand;

SEINARTSCOREENTITY_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinAI, Log, All);

/**
 * Compact tick context handed to BP graphs on every AI tick. Lets the author
 * read the current sim tick + delta without plumbing the world subsystem.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinAITickContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|AI")
	int32 CurrentTick = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|AI")
	FFixedPoint DeltaTime;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|AI")
	FSeinPlayerID OwnedPlayerID;
};

/**
 * Abstract AI controller. Instantiate subclasses on the sim host (local in
 * single-player) and register them with `USeinWorldSubsystem::RegisterAIController`;
 * the subsystem ticks them during CommandProcessing phase. Commands emitted
 * via `EmitCommand` flow through the lockstep txn log.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, ClassGroup = (SeinARTS),
	meta = (DisplayName = "AI Controller"))
class SEINARTSCOREENTITY_API USeinAIController : public UObject
{
	GENERATED_BODY()

public:
	/** Player this controller issues commands on behalf of. Set by
	 *  `USeinWorldSubsystem::RegisterAIController`. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|AI")
	FSeinPlayerID OwnedPlayerID;

	/** Transient world subsystem back-reference — set on registration. */
	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	/** Route GetWorld() through the cached WorldSubsystem so BP graphs in
	 *  AI subclasses can call WorldContext-tagged BPFLs without manually
	 *  wiring the World Context Object pin. */
	virtual UWorld* GetWorld() const override;

	// Lifecycle ------------------------------------------------------------

	/** Called once on registration. Override in BP/C++ to seed state. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|AI")
	void OnRegistered();
	virtual void OnRegistered_Implementation() {}

	/** Called once on unregistration (player left, match ended, etc.). */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|AI")
	void OnUnregistered();
	virtual void OnUnregistered_Implementation() {}

	/** Per-tick entry point. Override in BP/C++ to read sim state + emit
	 *  commands. Called by `USeinWorldSubsystem` during CommandProcessing
	 *  phase, so emitted commands process same-tick. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|AI")
	void Tick(const FSeinAITickContext& Context);
	virtual void Tick_Implementation(const FSeinAITickContext& /*Context*/) {}

	// Emit ----------------------------------------------------------------

	/** Drop a command into the lockstep buffer. Typically the AI author
	 *  fills `PlayerID = OwnedPlayerID` before calling. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|AI")
	void EmitCommand(const FSeinCommand& Command);

	/** Convenience for BP graphs: returns this controller's owned player ID. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|AI")
	FSeinPlayerID GetOwnedPlayerID() const { return OwnedPlayerID; }
};
