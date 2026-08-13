using UnrealBuildTool;

public class SeinARTSMovement : ModuleRules
{
    public SeinARTSMovement(ReadOnlyTargetRules Target) : base(Target)
    {
        // Keep movement codec/provider implementation helpers in distinct
        // translation units; Unreal unity merging breaks that isolation.
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "SeinARTSCore",
            "GameplayTags",   // FGameplayTag is exposed on the public mover/planner handle headers
            "SeinARTSCoreEntity",
            "SeinARTSNavigation" // FSeinPath and navigation request/result types are public API
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
