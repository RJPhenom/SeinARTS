/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToActionContinuationTestAccess.h
 * @brief   Non-shipping probes for the exact Move To continuation codec.
 */

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

class UObject;
class USeinAbility;
class USeinMovement;
class USeinMoveToAction;
class USeinMoveToProxy;
struct FSeinSnapshotLatentActionRecord;

namespace UE::SeinARTSTests
{
	/**
	 * Exercises the one production field-mapping helper without publishing the
	 * module-private payload schema to ordinary consumers.
	 */
	struct SEINARTSMOVEMENT_API
	FMoveToActionContinuationTestAccess
	{
		static void SeedEveryMappedField(
			USeinMoveToAction& Action,
			USeinMovement* Movement);

		static USeinMoveToAction* CloneMappedFields(
			const USeinMoveToAction& Source,
			UObject& Outer);

		static bool MappedFieldsEqual(
			const USeinMoveToAction& A,
			const USeinMoveToAction& B,
			FString& OutError);

		static bool DiagnosticsWereReset(
			const USeinMoveToAction& Action);

		static USeinMoveToProxy* GetObserver(
			const USeinMoveToAction& Action);

		static USeinMoveToAction* GetRunningAction(
			const USeinMoveToProxy& Proxy);

		static bool IsSilentlyDetached(
			const USeinMoveToProxy& Proxy);
	};

	enum class EMoveToContinuationMutation : uint8
	{
		InvalidWaypointCursor,
		ResolvedWithoutMovement,
		NonstandardCompletedRoute,
		EscapeCounterOutsideBound,
	};

	/**
	 * Decode, mutate, and canonically re-encode only the Move To payload.
	 * Core's test reseal helper must then authenticate the enclosing record and
	 * sequence so restore reaches module-owned staging.
	 */
	SEINARTSMOVEMENT_API bool
	MutateMoveToContinuationForTest(
		FSeinSnapshotLatentActionRecord& Record,
		EMoveToContinuationMutation Mutation,
		FString& OutError);

	/** Replace only the serialized OnCompleted route with another exact
	 *  generated callback name. Used to prove mixed-node payloads fail. */
	SEINARTSMOVEMENT_API bool
	ReplaceMoveToCompletedRouteForTest(
		FSeinSnapshotLatentActionRecord& Record,
		const FString& ReplacementFunction,
		FString& OutError);
}

#endif
