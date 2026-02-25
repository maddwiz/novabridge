using UnrealBuildTool;

public class NovaBridgeCore : ModuleRules
{
	public NovaBridgeCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"Json",
			"HTTPServer",
		});
	}
}
