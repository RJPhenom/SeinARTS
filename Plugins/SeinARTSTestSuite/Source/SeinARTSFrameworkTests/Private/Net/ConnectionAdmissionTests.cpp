/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         ConnectionAdmissionTests.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Qualifies the optional pre-relay connection admission seam.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "TestGameInstance.h"

#include "GameFramework/PlayerController.h"
#include "SeinConnectionAdmission.h"
#include "SeinNetSubsystem.h"

namespace UE::SeinARTSTests
{
	TEST(ConnectionAdmissionIsOwnerScopedAndExactSeat,
		"SeinARTS.Unit.Network.ConnectionAdmission")
	{
		FActorTestSpawner Spawner;
		Spawner.InitializeGameSubsystems();
		USeinNetSubsystem* Net = Spawner.GetGameInstance()
			->GetSubsystem<USeinNetSubsystem>();
		ASSERT_THAT(IsNotNull(Net));
		const FSeinMatchInstanceID MatchID(FGuid(1, 2, 3, 4));
		const FSeinNetworkParticipantID ParticipantID(FGuid(5, 6, 7, 8));
		Net->SetConnectionAdmissionBindingForTests(
			MatchID, ParticipantID, FSeinPlayerID(3));

		int32 AuthorizerCalls = 0;
		FString ObservedAdmission;
		FString ObservedOptions;
		const FName Owner(TEXT("SeinARTS.Tests.Admission"));
		ASSERT_THAT(IsTrue(Net->RegisterConnectionAdmissionAuthorizer(
			Owner,
			FSeinConnectionAdmissionAuthorizer::CreateLambda(
				[&](const FSeinConnectionAdmissionRequest& Request)
				{
					++AuthorizerCalls;
					ObservedAdmission = Request.AdmissionID;
					ObservedOptions = Request.Options;
					FSeinConnectionAdmissionDecision Decision;
					Decision.bAccepted = true;
					Decision.MatchID = MatchID;
					Decision.ParticipantID = ParticipantID;
					Decision.AssignedSlot = FSeinPlayerID(3);
					Decision.ErrorMessage.Reset();
					return Decision;
				}))));
		ASSERT_THAT(IsFalse(Net->RegisterConnectionAdmissionAuthorizer(
			TEXT("SeinARTS.Tests.OtherOwner"),
			FSeinConnectionAdmissionAuthorizer::CreateLambda(
				[](const FSeinConnectionAdmissionRequest&)
				{
					return FSeinConnectionAdmissionDecision();
				}))));

		const FString Options =
			TEXT("?SeinAdmission=admission-1?Secret=must-not-be-retained");
		FString Error;
		ASSERT_THAT(IsTrue(Net->AuthorizeIncomingConnection(
			Options, TEXT("127.0.0.1"), FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(IsTrue(Error.IsEmpty()));
		ASSERT_THAT(AreEqual(1, AuthorizerCalls));
		ASSERT_THAT(AreEqual(FString(TEXT("admission-1")), ObservedAdmission));
		ASSERT_THAT(AreEqual(Options, ObservedOptions));

		ASSERT_THAT(IsTrue(Net->AuthorizeIncomingConnection(
			Options, TEXT("127.0.0.1"), FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(AreEqual(1, AuthorizerCalls));
		ASSERT_THAT(IsFalse(Net->AuthorizeIncomingConnection(
			Options, TEXT("127.0.0.2"), FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(AreEqual(1, AuthorizerCalls));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("identity changed"))));

		const FString DuplicateSeatOptions =
			TEXT("?SeinAdmission=admission-2?Secret=other");
		ASSERT_THAT(IsFalse(Net->AuthorizeIncomingConnection(
			DuplicateSeatOptions, TEXT("127.0.0.2"),
			FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(AreEqual(2, AuthorizerCalls));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("already pending"))));

		APlayerController& Controller = Spawner.SpawnActor<APlayerController>();
		FSeinPlayerID Slot;
		ASSERT_THAT(IsTrue(Net->ConsumeAuthorizedConnection(
			&Controller, Options, FUniqueNetIdRepl(), Slot, Error)));
		ASSERT_THAT(IsTrue(Slot == FSeinPlayerID(3)));
		ASSERT_THAT(IsFalse(Net->AuthorizeIncomingConnection(
			DuplicateSeatOptions, TEXT("127.0.0.2"),
			FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("already connected"))));
		ASSERT_THAT(IsFalse(Net->ConsumeAuthorizedConnection(
			&Controller, Options, FUniqueNetIdRepl(), Slot, Error)));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("expired or changed"))));

		Net->UnregisterConnectionAdmissionAuthorizer(
			TEXT("SeinARTS.Tests.OtherOwner"));
		ASSERT_THAT(IsTrue(Net->HasConnectionAdmissionAuthorizer()));
		Net->UnregisterConnectionAdmissionAuthorizer(Owner);
		ASSERT_THAT(IsFalse(Net->HasConnectionAdmissionAuthorizer()));
	}

	TEST(ConnectionAdmissionFailsClosedBeforeCaching,
		"SeinARTS.Unit.Network.ConnectionAdmission")
	{
		FActorTestSpawner Spawner;
		Spawner.InitializeGameSubsystems();
		USeinNetSubsystem* Net = Spawner.GetGameInstance()
			->GetSubsystem<USeinNetSubsystem>();
		ASSERT_THAT(IsNotNull(Net));

		int32 AuthorizerCalls = 0;
		const FName Owner(TEXT("SeinARTS.Tests.Rejection"));
		ASSERT_THAT(IsTrue(Net->RegisterConnectionAdmissionAuthorizer(
			Owner,
			FSeinConnectionAdmissionAuthorizer::CreateLambda(
				[&](const FSeinConnectionAdmissionRequest&)
				{
					++AuthorizerCalls;
					FSeinConnectionAdmissionDecision Decision;
					Decision.ErrorMessage = TEXT("Safe rejection");
					return Decision;
				}))));

		FString Error;
		ASSERT_THAT(IsFalse(Net->AuthorizeIncomingConnection(
			TEXT("?Other=1"), TEXT("127.0.0.1"),
			FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(AreEqual(0, AuthorizerCalls));
		ASSERT_THAT(IsTrue(Error.Contains(TEXT("missing"))));
		ASSERT_THAT(IsFalse(Net->AuthorizeIncomingConnection(
			FString::ChrN(8193, TEXT('x')), TEXT("127.0.0.1"),
			FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(AreEqual(0, AuthorizerCalls));

		const FString Options = TEXT("?SeinAdmission=reject-1");
		ASSERT_THAT(IsFalse(Net->AuthorizeIncomingConnection(
			Options, TEXT("127.0.0.1"), FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(AreEqual(1, AuthorizerCalls));
		ASSERT_THAT(AreEqual(FString(TEXT("Safe rejection")), Error));
		ASSERT_THAT(IsFalse(Net->AuthorizeIncomingConnection(
			Options, TEXT("127.0.0.1"), FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(AreEqual(2, AuthorizerCalls));

		Net->UnregisterConnectionAdmissionAuthorizer(Owner);
	}
}
