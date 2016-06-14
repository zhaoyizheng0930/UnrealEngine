// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class VREditor : ModuleRules
	{
        public VREditor(TargetInfo Target)
		{
			PrivateDependencyModuleNames.AddRange(
                new string[] {
				    "Core",
				    "CoreUObject",
				    "Engine",
                    "InputCore",
				    "Slate",
					"SlateCore",
                    "EditorStyle",
				    "UnrealEd",
					"UMG",
					"LevelEditor",
					"HeadMountedDisplay",
                    "ViewportInteraction"
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"PlacementMode",
				}
			);

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
				}
			);
		}
	}
}