/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationCanonicalStateProvider.h
 * @brief   Module-lifetime registration entry point for navigation state.
 */

#pragma once

#include "Serialization/SeinCanonicalStateRegistry.h"

FSeinCanonicalStateRegistrationHandle
SeinRegisterNavigationCanonicalStateProvider(FString& OutError);
