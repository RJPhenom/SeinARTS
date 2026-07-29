// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

public class SeinARTSCoverSquad : ModuleRules
{
	public SeinARTSCoverSquad(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"SeinARTSCoreEntity",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",
			"GameplayTags",
			"SeinARTSCore",
			"SeinARTSCover",
			"SeinARTSSquad",
			"SeinARTSFramework",       // FormationPreviewSubsystem uses ASeinPlayerController + USeinTargeterSubsystem
			"SeinARTSFogOfWar",        // Cover queries gate by per-observer fog visibility
		});
	}
}
