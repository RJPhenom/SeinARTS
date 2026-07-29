/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarCanonicalStateProvider.h
 * @brief   Module-lifetime registration entry points for exact fog state.
 */

#pragma once

#include "Serialization/SeinCanonicalStateRegistry.h"
#include "Serialization/SeinFogOfWarStateCodecRegistry.h"

FSeinCanonicalStateRegistrationHandle
SeinRegisterFogOfWarCanonicalStateProvider(FString& OutError);

FSeinFogOfWarStateCodecRegistrationHandle
SeinRegisterDefaultFogOfWarStateCodec(FString& OutError);

