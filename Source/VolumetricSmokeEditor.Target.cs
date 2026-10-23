using UnrealBuildTool;

public class VolumetricSmokeEditorTarget : TargetRules
{
	public VolumetricSmokeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "SmokeLab", "SmokeLabShaders" });
	}
}
