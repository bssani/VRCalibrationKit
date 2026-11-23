using UnrealBuildTool;

public class VRCalibrationKitEditorTarget : TargetRules
{
    public VRCalibrationKitEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
        ExtraModuleNames.Add("VRCalibrationKit");
    }
}
