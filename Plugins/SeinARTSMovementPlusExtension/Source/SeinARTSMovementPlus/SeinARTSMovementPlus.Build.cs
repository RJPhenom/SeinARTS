using UnrealBuildTool;

public class SeinARTSMovementPlus : ModuleRules
{
    public SeinARTSMovementPlus(ReadOnlyTargetRules Target) : base(Target)
    {
        // Vehicle implementations reuse private fixed-point helper names;
        // unity merging collapses their anonymous namespaces and is invalid.
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[] {
            "SeinARTSCore",
            "SeinARTSCoreEntity",
            "SeinARTSMovement"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSNavigation",
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
