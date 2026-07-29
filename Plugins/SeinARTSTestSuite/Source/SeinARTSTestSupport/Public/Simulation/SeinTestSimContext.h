/**
 * Non-shipping test access to Core's exact-world simulation context.
 * Production extensions cannot mint this capability.
 */

#pragma once

#include "Core/SeinSimContext.h"

#if !WITH_DEV_AUTOMATION_TESTS
#error SeinTestSimContext is available only to development automation tests.
#endif

struct FSeinSimContextTestAccess
{
	static FSeinSimContextScope Enter(const USeinWorldSubsystem& World)
	{
		return FSeinSimContextScope(World);
	}
};
