// Copyright Pr_UEDraw. All Rights Reserved.

using UnrealBuildTool;

public class USCADAWidgetEditor : ModuleRules
{
	public USCADAWidgetEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"USCADAWidget"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"UMG",
			"UnrealEd",
			"UMGEditor"
		});
	}
}
