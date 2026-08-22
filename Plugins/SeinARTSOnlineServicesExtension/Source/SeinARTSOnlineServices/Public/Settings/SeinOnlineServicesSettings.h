/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesSettings.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Project settings for selecting and bounding the SOS provider.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Templates/SubclassOf.h"
#include "SeinOnlineServicesSettings.generated.h"

class USeinOnlineServicesProvider;

/** Configures the optional SeinARTS Online Services extension. */
UCLASS(Config = Game, DefaultConfig,
	meta = (DisplayName = "SeinARTS Online Services"))
class SEINARTSONLINESERVICES_API USeinOnlineServicesSettings
	: public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USeinOnlineServicesSettings();

	/**
	 * Backend adapter created for each game instance.
	 *
	 * The shipped default is the in-memory Loopback provider. Invalid or
	 * abstract classes fail visibly. Development clients may fall back to
	 * Loopback only when Allow Development Loopback permits it; Shipping,
	 * dedicated servers, and required authenticated admission always fail closed.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Provider",
		meta = (DisplayName = "Provider Class"))
	TSubclassOf<USeinOnlineServicesProvider> ProviderClass;

	/**
	 * Allows the insecure in-memory Loopback provider in local development.
	 * Loopback is always refused in Shipping, dedicated-server, and required
	 * authenticated-admission configurations regardless of this value.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Provider",
		meta = (DisplayName = "Allow Development Loopback"))
	bool bAllowDevelopmentLoopback = true;

	/**
	 * Requires every remote connection's non-secret admission identity and
	 * authenticated transport identity to resolve to provider-owned account,
	 * match, participant, and exact-seat evidence. Bearer credentials are never
	 * accepted in connection URLs because Unreal logs those options.
	 *
	 * Disabled by default to preserve existing local PIE and direct-connect
	 * projects. Production online servers should enable it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Provider",
		meta = (DisplayName = "Require Authenticated Connection Admission"))
	bool bRequireAuthenticatedConnectionAdmission = false;

	/** Maximum pending and retained requests owned by one game instance. */
	UPROPERTY(Config, EditAnywhere, Category = "Limits",
		meta = (ClampMin = "16", ClampMax = "65536"))
	int32 MaxRequestRecords = 2048;

	/** Maximum completed results retained for later consumption. */
	UPROPERTY(Config, EditAnywhere, Category = "Limits",
		meta = (ClampMin = "1", ClampMax = "65536"))
	int32 MaxRetainedCompletedRequests = 512;

	virtual FName GetCategoryName() const override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
};
