/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinContainmentStateValidation.h
 * @author       RJ Macklem
 * @created      13 Aug 2026
 * @latest       13 Aug 2026
 * @brief        Declares structural validation for authoritative containment state.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "Containers/ArrayView.h"
#include "Core/SeinEntityHandle.h"
#include "Templates/Function.h"

struct FSeinAttachmentSpec;
struct FSeinContainmentData;
struct FSeinContainmentMemberData;

namespace UE::SeinARTSCoreEntity
{
	bool ValidateContainmentContainer(
		FSeinEntityHandle ContainerHandle,
		const FSeinContainmentData& Container,
		TFunctionRef<bool(FSeinEntityHandle)> IsEntityAvailable,
		TFunctionRef<const FSeinContainmentMemberData*(FSeinEntityHandle)>
			FindMember,
		const FSeinAttachmentSpec* Attachment,
		FString& OutError);

	bool ValidateContainmentState(
		TConstArrayView<FSeinEntityHandle> Entities,
		TFunctionRef<bool(FSeinEntityHandle)> IsEntityValid,
		TFunctionRef<const FSeinContainmentData*(FSeinEntityHandle)>
			FindContainer,
		TFunctionRef<const FSeinContainmentMemberData*(FSeinEntityHandle)>
			FindMember,
		TFunctionRef<const FSeinAttachmentSpec*(FSeinEntityHandle)>
			FindAttachment,
		FString& OutError);
}
