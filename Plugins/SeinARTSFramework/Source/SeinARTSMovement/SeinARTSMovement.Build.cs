using UnrealBuildTool;

public class SeinARTSMovement : ModuleRules
{
    public SeinARTSMovement(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[] {
            "GameplayTags",   // FGameplayTag is exposed on the public mover/planner handle headers
            "SeinARTSCoreEntity",
            "SeinARTSNavigation" // FSeinPath and navigation request/result types are public API
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore"
        });

        // Editor-only deps: the active-move debug ticker reaches into the
        // editor viewport iterator to gate per-world drawing on the editor's
        // Navigation showflag (mirrors USeinARTSNavigationModule's pattern).
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] {
                "UnrealEd"
            });
        }
    }
}
