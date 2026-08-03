// SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.

using UnrealBuildTool;

// Optional integration module. Plugin-level dependencies guarantee that the
// Framework, Cover, and Squad modules are present before this module loads.
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
			"SeinARTSCore",
			"SeinARTSCoreEntity",
			"SeinARTSSquad",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"SeinARTSCover",
		});
	}
}
