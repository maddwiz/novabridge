using UnrealBuildTool;

public class NovaBridgeRuntime : ModuleRules
{
	public NovaBridgeRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTPServer",
			"Json",
			"JsonUtilities",
			"NovaBridgeCore",
		});
	}
}
