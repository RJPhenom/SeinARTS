/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSSquadSettings.h
 * @brief   Settings for the opt-in SeinARTS Squad Extension. Separate
 *          UDeveloperSettings page ("SeinARTS Squad Extension") so squad
 *          configuration lives entirely inside the extension plugin — the
 *          base framework's USeinARTSCoreSettings carries no squad fields,
 *          keeping the extension fully strippable.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"
#include "SeinARTSSquadSettings.generated.h"

class USeinCommandBrokerResolver;

/**
 * Settings for the SeinARTS Squad Extension. Configure under
 * Project Settings > Plugins > SeinARTS Squad Extension.
 *
 * Lives in the SeinARTSSquad module (not the base framework) so the squad
 * system's configuration surface is owned by the extension. When the Squad
 * Extension is not installed, this page simply doesn't exist.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SeinARTS Squad Extension"))
class SEINARTSSQUAD_API USeinARTSSquadSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	/**
	 * Project-wide default resolver class instantiated on every squad's
	 * CommandBroker at lazy-init time. Consulted by `FSeinSquadSystem` when a
	 * squad's per-squad `FSeinSquadComponent::DispatchResolverClass` is empty;
	 * the per-squad override always wins when set.
	 *
	 * Empty path = use the framework-default `USeinSquadDispatchResolver`
	 * (plain per-slot formation, no cover-snap). Projects wanting cover-aware
	 * squad dispatch out of the box point this at the Cover Extension's
	 * `/Script/SeinARTSCoverSquad.SeinCoverAwareSquadDispatchResolver`.
	 *
	 * Soft path so the Squad Extension stays decoupled from the Cover Extension
	 * — the bridge resolver only loads when this path is set AND the cover
	 * modules are present.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Dispatch",
		meta = (DisplayName = "Default Squad Dispatch Resolver Class"))
	TSoftClassPtr<USeinCommandBrokerResolver> DefaultSquadDispatchResolverClass;

	/** Whether the separate squads of ONE multi-squad order pace each other in transit.
	 *
	 *  Cohesion normally keeps a single squad's members together (a straggler's squadmates hold up
	 *  for it). This extends that ONE level up: when on, the squads of a single multi-squad move also
	 *  pace each other, so a fast squad eases off and the laggard squads catch up, keeping the whole
	 *  ordered body together instead of the fast squad racing ahead and the formation stringing out.
	 *  A squad never lets its own leaders outrun its own stragglers to do this. Off = each squad keeps
	 *  its own pace and personality (they may separate in transit). Default on. Only affects orders
	 *  that span more than one squad; loose units and lone squads are unaffected.
	 *
	 *  Consumed by the framework avoidance model (the squad system stamps it onto each squad's command
	 *  broker; the base never reads this extension setting directly). Sim-affecting, so it is registered
	 *  into the lockstep config-parity fingerprint via the squad module's fingerprint contributor. */
	UPROPERTY(Config, EditAnywhere, Category = "Dispatch",
		meta = (DisplayName = "Pace Squads Together"))
	bool bPaceSquadsTogether = true;

	// UDeveloperSettings Interface
	virtual FName GetCategoryName() const override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
};
