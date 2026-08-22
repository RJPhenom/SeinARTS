/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinOnlineServicesProvider.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Declares the native backend-adapter contract owned by SOS.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/Object.h"
#include "Types/SeinOnlineServicesTypes.h"
#include "SeinOnlineServicesProvider.generated.h"

class USeinOnlineServicesSubsystem;
struct FSeinOnlineProviderLifecycleState;

/** Non-secret correlation material used during synchronous server admission. */
struct SEINARTSONLINESERVICES_API FSeinOnlineConnectionAdmissionRequest
{
	/** Non-secret provider-issued identity carried by the SeinAdmission URL option. */
	FString AdmissionID;

	/** Authenticated transport identity supplied by Unreal, when available. */
	FString PlatformIdentityType;

	/** Provider-scoped transport identity value supplied by Unreal. */
	FString PlatformIdentityValue;
};

/** Sanitized provider decision for an incoming connection. */
struct SEINARTSONLINESERVICES_API FSeinOnlineConnectionAdmissionDecision
{
	bool bAccepted = false;
	FSeinMatchInstanceID MatchID;
	FSeinNetworkParticipantID ParticipantID;
	FSeinPlayerID AssignedSlot;
	FSeinOnlineError Error;
};

/** Move-only provider completion. Providers may invoke it from any thread. */
using FSeinOnlineProviderCompletion =
	TUniqueFunction<void(FSeinOnlineProviderResponse&&)>;

/**
 * Native base class for an SOS backend adapter.
 *
 * Providers own service I/O and backend-specific state. The subsystem owns
 * request identity, schema validation, game-thread deferral, cancellation,
 * generation guards, and bounded result retention.
 */
UCLASS(Abstract)
class SEINARTSONLINESERVICES_API USeinOnlineServicesProvider : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Initializes this provider for one game-instance subsystem.
	 * Lifecycle setup and partial-initialization cleanup are base-owned.
	 */
	bool Initialize(
		USeinOnlineServicesSubsystem& InOwner,
		FSeinOnlineError& OutError);

	/**
	 * Cancels provider I/O and releases all backend-owned runtime state.
	 *
	 * This non-overridable wrapper requires ShutdownProvider to synchronously
	 * join provider workers and destroy every retained completion before return.
	 * Violations fail a checked build instead of leaving executable callback
	 * thunks alive across module unload.
	 */
	void Shutdown();

	/** Stable backend namespace used by opaque identifiers. */
	virtual FName GetProviderName() const PURE_VIRTUAL(
		USeinOnlineServicesProvider::GetProviderName,
		return NAME_None;);

	/** True when this adapter implements Operation. */
	virtual bool SupportsOperation(ESeinOnlineOperation Operation) const
		PURE_VIRTUAL(
			USeinOnlineServicesProvider::SupportsOperation,
			return false;);

	/**
	 * Begins one already-validated request. Completion must fire at most once.
	 * The subsystem safely accepts synchronous or cross-thread callbacks.
	 */
	void BeginRequest(
		const FSeinOnlineProviderRequest& Request,
		FSeinOnlineProviderCompletion&& Completion);

	/**
	 * Attempts to stop provider-owned work before it commits.
	 *
	 * Return true only when the operation cannot commit and the provider will
	 * not invoke its completion. False leaves the original result authoritative.
	 */
	virtual bool CancelRequest(FSeinOnlineRequestHandle Handle);

	/**
	 * Confirms backend authentication for a trusted-server request.
	 *
	 * A request's authority marker is routing context only. Production adapters
	 * must verify server credentials independently before returning true.
	 */
	virtual bool IsTrustedServerRequestAuthenticated(
		const FSeinOnlineProviderRequest& Request) const;

	/**
	 * Resolves a non-secret admission identity to an authenticated match seat.
	 * Production adapters must bind it to backend-preverified account or
	 * transport identity evidence. Login URL options are logged by Unreal and
	 * therefore must never contain bearer credentials.
	 */
	virtual FSeinOnlineConnectionAdmissionDecision AuthorizeConnection(
		const FSeinOnlineConnectionAdmissionRequest& Request);

protected:
	/** Initializes adapter-specific state after the base lifecycle is armed. */
	virtual bool InitializeProvider(FSeinOnlineError& OutError);

	/** Begins provider-owned work for one validated request. */
	virtual void BeginRequestProvider(
		const FSeinOnlineProviderRequest& Request,
		FSeinOnlineProviderCompletion&& Completion)
		PURE_VIRTUAL(USeinOnlineServicesProvider::BeginRequestProvider, );

	/**
	 * Stops work and destroys every retained completion before returning.
	 * Implementations must join callback-producing worker threads here.
	 */
	virtual void ShutdownProvider()
		PURE_VIRTUAL(USeinOnlineServicesProvider::ShutdownProvider, );

	/** Owning game-instance facade. Valid between Initialize and Shutdown. */
	TWeakObjectPtr<USeinOnlineServicesSubsystem> Owner;

private:
	TSharedPtr<FSeinOnlineProviderLifecycleState> LifecycleState;
};
