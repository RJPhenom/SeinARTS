/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementStateCoverage.h
 * @brief   Exact-state coverage claims for native movement policy layers.
 */

#pragma once

#include "CoreMinimal.h"
#include "Serialization/SeinCanonicalStateRegistry.h"

class UClass;

/**
 * How one native class layer accounts for future-affecting instance state.
 *
 * Blueprint-generated layers need no claim: their ordinary deterministic,
 * non-transient variables are captured automatically by reflection.
 */
enum class ESeinMovementStateCoverage : uint8
{
	/** This native layer adds no future-affecting instance state. */
	Stateless,

	/** Every future-affecting field added by this layer is a UPROPERTY. */
	ReflectedComplete,

	/**
	 * The layer also owns opaque native state. Every listed canonical provider
	 * is required in the match contract and restores that state.
	 */
	Supplemental,
};

struct SEINARTSMOVEMENT_API FSeinMovementStateCoverageDescriptor
{
	/** Exact native USeinMovement or USeinAvoidance class layer being claimed. */
	const UClass* NativeClass = nullptr;

	ESeinMovementStateCoverage Coverage =
		ESeinMovementStateCoverage::Stateless;

	/** Required only for Supplemental; canonicalized and deduplicated. */
	TArray<FSeinCanonicalStateKey> RequiredProviders;
};

/** Move-only, reload-safe claim owned by the class's implementation module. */
class SEINARTSMOVEMENT_API FSeinMovementStateCoverageRegistrationHandle
{
public:
	FSeinMovementStateCoverageRegistrationHandle() = default;
	~FSeinMovementStateCoverageRegistrationHandle();

	FSeinMovementStateCoverageRegistrationHandle(
		const FSeinMovementStateCoverageRegistrationHandle&) = delete;
	FSeinMovementStateCoverageRegistrationHandle& operator=(
		const FSeinMovementStateCoverageRegistrationHandle&) = delete;

	FSeinMovementStateCoverageRegistrationHandle(
		FSeinMovementStateCoverageRegistrationHandle&& Other) noexcept;
	FSeinMovementStateCoverageRegistrationHandle& operator=(
		FSeinMovementStateCoverageRegistrationHandle&& Other) noexcept;

	bool IsValid() const { return Token != 0; }
	void Reset();

private:
	explicit FSeinMovementStateCoverageRegistrationHandle(uint64 InToken)
		: Token(InToken)
	{
	}

	uint64 Token = 0;
	friend class FSeinMovementStateCoverageRegistry;
};

/**
 * Process registry for native movement/avoidance state coverage.
 *
 * Duplicate identical claims from the same owner coexist during hot reload.
 * A conflicting claim for an exact class fails closed. Adding or removing a
 * distinct claim refreshes the framework's canonical provider generation, so
 * already-frozen worlds cannot silently continue under a changed contract.
 */
class SEINARTSMOVEMENT_API FSeinMovementStateCoverageRegistry
{
public:
	static FSeinMovementStateCoverageRegistrationHandle Register(
		FName OwnerModuleId,
		const FSeinMovementStateCoverageDescriptor& Descriptor,
		FString* OutError = nullptr);

	static bool Unregister(
		FSeinMovementStateCoverageRegistrationHandle& Handle);

	static int32 GetRegisteredClassCount();

private:
	static bool UnregisterToken(uint64 Token);
	friend class FSeinMovementStateCoverageRegistrationHandle;
};
