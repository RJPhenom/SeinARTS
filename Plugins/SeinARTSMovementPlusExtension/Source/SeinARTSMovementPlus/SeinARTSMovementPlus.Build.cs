using UnrealBuildTool;

public class SeinARTSMovementPlus : ModuleRules
{
    public SeinARTSMovementPlus(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore", "SeinARTSCoreEntity",
            "SeinARTSNavigation", "SeinARTSMovement",
            "GameplayTags"
        });

        // Editor-only: the moved movement classes gate per-tick steering debug
        // viz on the main module's exported show-flag helpers (which themselves
        // reach into the editor viewport iterator). Mirrors SeinARTSMovement.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd"
            });
        }
    }
}
