// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SimplygonSwarm : ModuleRules
{
	public SimplygonSwarm(TargetInfo Target)
	{
		BinariesSubFolder = "NotForLicensees";

		PublicIncludePaths.Add("Developer/SimplygonSwarm/Public");
        PrivateIncludePaths.Add("Developer/SimplygonSwarm/Private");

        PublicDependencyModuleNames.AddRange(
        new string[] { 
				"Core",
				"CoreUObject",
				"InputCore",
				"Json",
				"RHI"
			}
        );

        PrivateDependencyModuleNames.AddRange(
        new string[] { 
				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore",
				"RawMesh",
                "MeshBoneReduction",
                "ImageWrapper",
                "HTTP",
                "Json",
                "UnrealEd",
                "MaterialUtilities"
			}
        );                

        PrivateIncludePathModuleNames.AddRange(
        new string[] { 
				"MeshUtilities",
				"MaterialUtilities",
                "SimplygonMeshReduction"
			}
        );

		AddThirdPartyPrivateStaticDependencies(Target, "Simplygon");
		AddThirdPartyPrivateStaticDependencies(Target, "SSF");
		AddThirdPartyPrivateDynamicDependencies(Target, "PropertyEditor");
	}
}
