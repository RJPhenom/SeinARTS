/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineLoopbackProvider.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Declares the complete in-process reference provider for SOS.
 *
 *               Loopback qualifies API and state-machine behavior only. It is
 *               not secure, durable, cross-process, or suitable for production.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Provider/SeinOnlineServicesProvider.h"
#include "SeinOnlineLoopbackProvider.generated.h"

struct FSeinOnlineLoopbackBackend;

/** In-memory provider implementing every shipped SOS operation. */
UCLASS()
class SEINARTSONLINESERVICES_API USeinOnlineLoopbackProvider final
	: public USeinOnlineServicesProvider
{
	GENERATED_BODY()

public:
	virtual FName GetProviderName() const override;
	virtual bool SupportsOperation(ESeinOnlineOperation Operation) const override;
	virtual bool IsTrustedServerRequestAuthenticated(
		const FSeinOnlineProviderRequest& Request) const override;
	virtual FSeinOnlineConnectionAdmissionDecision AuthorizeConnection(
		const FSeinOnlineConnectionAdmissionRequest& Request) override;

#if WITH_DEV_AUTOMATION_TESTS
	/** Overrides the provider clock used by reconnect-expiry tests. */
	void SetUtcNowUnixMillisecondsForTests(int64 UnixMilliseconds);

	/** Restores the real UTC clock after a reconnect-expiry test. */
	void ClearUtcNowOverrideForTests();
#endif

private:
	virtual bool InitializeProvider(FSeinOnlineError& OutError) override;
	virtual void BeginRequestProvider(
		const FSeinOnlineProviderRequest& Request,
		FSeinOnlineProviderCompletion&& Completion) override;
	virtual void ShutdownProvider() override;

	FSeinOnlineProviderResponse ProcessRequest(
		const FSeinOnlineProviderRequest& Request);
	int64 GetUtcNowUnixMilliseconds() const;

	TSharedPtr<FSeinOnlineLoopbackBackend> Backend;
	TMap<int32, FSeinOnlineOpaqueID> LocalAccountsByUser;
	TSet<FString> AuthenticatedAccounts;

#if WITH_DEV_AUTOMATION_TESTS
	bool bHasUtcNowOverride = false;
	int64 UtcNowOverride = 0;
#endif
};
