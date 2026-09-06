/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinARTSSquadEditorModule.cpp
 * @author       RJ Macklem
 * @created      4 Sep 2026
 * @latest       4 Sep 2026
 * @brief        Presents Squad-owned settings on the shared SeinARTS page.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Modules/ModuleManager.h"

#include "SeinARTSEditorModule.h"
#include "SeinARTSSquadSettings.h"

namespace
{
	const FName GSquadSettingsKey(TEXT("SeinARTSSquadSettings"));
}

class FSeinARTSSquadEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (FSeinARTSEditorModule* EditorModule =
				FModuleManager::LoadModulePtr<FSeinARTSEditorModule>(
					TEXT("SeinARTSEditor")))
		{
			EditorModule->RegisterSettingsCategoryContribution(
				GSquadSettingsKey,
				TEXT("Squad"),
				GetMutableDefault<USeinARTSSquadSettings>(),
				{
					{
						GET_MEMBER_NAME_CHECKED(
							USeinARTSSquadSettings,
							bPaceSquadsTogether),
						false
					},
					{
						GET_MEMBER_NAME_CHECKED(
							USeinARTSSquadSettings,
							DefaultSquadDispatchResolverClass),
						false
					},
				});
		}
	}

	virtual void ShutdownModule() override
	{
		if (FSeinARTSEditorModule* EditorModule =
				FModuleManager::GetModulePtr<FSeinARTSEditorModule>(
					TEXT("SeinARTSEditor")))
		{
			EditorModule->UnregisterSettingsCategoryContribution(
				GSquadSettingsKey);
		}
	}
};

IMPLEMENT_MODULE(FSeinARTSSquadEditorModule, SeinARTSSquadEditor)
