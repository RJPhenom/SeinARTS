// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

public class SeinARTSSquadEditor : ModuleRules
{
	public SeinARTSSquadEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"SeinARTSSquad",
			"SeinARTSEditor"
		});
	}
}
