/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToActionCodec.h
 * @brief   Module-owned registration for the exact Move To continuation.
 */

#pragma once

#include "Serialization/SeinLatentActionCodecRegistry.h"

FSeinLatentActionCodecRegistrationHandle
SeinRegisterMoveToActionCodec(FString& OutError);
