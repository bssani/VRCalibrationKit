using UnrealBuildTool;

public class VRCalibrationKit : ModuleRules
{
    public VRCalibrationKit(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "HeadMountedDisplay", "VRCalibrationCore" });
        PrivateDependencyModuleNames.Add("XRBase");
    }
}
