// Copyright Pr_UEDraw. All Rights Reserved.

using UnrealBuildTool;

public class USCADAWidgetEditor : ModuleRules
{
	public USCADAWidgetEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"InputCore",
			"UMG",
			"UMGEditor",
			"UnrealEd",
			"XmlParser",
			"DeveloperSettings",
			"USCADAWidget"
		});
	}
}
