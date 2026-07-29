#pragma once

#include "CoreMinimal.h"

/**
 * Core-wide game-thread guard shared by executable state-provider registries.
 * It prevents one registry's callback from mutating another registry and
 * triggering teardown inside capture/stage/commit.
 */
class FSeinStateProviderTransactionScope
{
public:
	FSeinStateProviderTransactionScope();
	~FSeinStateProviderTransactionScope();

	FSeinStateProviderTransactionScope(
		const FSeinStateProviderTransactionScope&) = delete;
	FSeinStateProviderTransactionScope& operator=(
		const FSeinStateProviderTransactionScope&) = delete;

	static bool IsActive();

private:
	static int32& Depth();
};
