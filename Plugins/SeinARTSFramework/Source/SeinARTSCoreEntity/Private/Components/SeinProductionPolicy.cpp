/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinProductionPolicy.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Validates and hashes production policy state in canonical class order.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
#include "Components/SeinProductionPolicy.h"

bool FSeinProductionQueueSettings::IsValid() const
{
	return static_cast<uint8>(QueuePolicy) <= static_cast<uint8>(ESeinProductionQueuePolicy::FixedAmountPerPlayer)
		&& QueueAmount >= 1;
}

bool FSeinProductionQueueSettings::IsPlayerScoped() const
{
	return QueuePolicy == ESeinProductionQueuePolicy::OncePerPlayer
		|| QueuePolicy == ESeinProductionQueuePolicy::FixedAmountPerPlayer;
}

int32 FSeinProductionQueueSettings::GetLimit() const
{
	switch (QueuePolicy)
	{
	case ESeinProductionQueuePolicy::OncePerProductionUnit:
	case ESeinProductionQueuePolicy::OncePerPlayer: return 1;
	case ESeinProductionQueuePolicy::FixedAmountPerProductionUnit:
	case ESeinProductionQueuePolicy::FixedAmountPerPlayer: return QueueAmount;
	default: return 0;
	}
}

bool FSeinProductionPolicyState::IsValid() const
{
	for (const auto& Pair : Completed)
	{
		if (!Pair.Key.StartsWith(TEXT("/")) || Pair.Value <= 0) return false;
	}
	for (const auto& Pair : Overrides)
	{
		if (!Pair.Key.StartsWith(TEXT("/")) || !Pair.Value.IsValid()) return false;
	}
	return true;
}

uint32 FSeinProductionPolicyState::ComputeHash() const
{
	TArray<FString> Keys;
	Completed.GetKeys(Keys);
	Keys.Sort();
	uint32 Hash = GetTypeHash(Keys.Num());
	for (const FString& Key : Keys)
	{
		Hash = HashCombine(Hash, GetTypeHash(Key));
		Hash = HashCombine(Hash, GetTypeHash(Completed.FindChecked(Key)));
	}
	Overrides.GetKeys(Keys);
	Keys.Sort();
	Hash = HashCombine(Hash, GetTypeHash(Keys.Num()));
	for (const FString& Key : Keys)
	{
		const auto& Settings = Overrides.FindChecked(Key);
		Hash = HashCombine(Hash, GetTypeHash(Key));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Settings.QueuePolicy)));
		Hash = HashCombine(Hash, GetTypeHash(Settings.QueueAmount));
	}
	return Hash;
}
