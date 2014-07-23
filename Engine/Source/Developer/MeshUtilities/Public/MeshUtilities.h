// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ModuleInterface.h"

/** The maximum index for a material in a mesh */
#define MAX_MESH_MATERIAL_INDEX 64

/**
 * Mesh reduction interface.
 */
class IMeshReduction
{
public:
	/**
	 * Reduces the raw mesh using the provided reduction settings.
	 * @param OutReducedMesh - Upon return contains the reduced mesh.
	 * @param OutMaxDeviation - Upon return contains the maximum distance by which the reduced mesh deviates from the original.
	 * @param InMesh - The mesh to reduce.
	 * @param ReductionSettings - Setting with which to reduce the mesh.
	 */
	virtual void Reduce(
		struct FRawMesh& OutReducedMesh,
		float& OutMaxDeviation,
		const struct FRawMesh& InMesh,
		const struct FMeshReductionSettings& ReductionSettings
		) = 0;
	/**
	 * Reduces the provided skeletal mesh.
	 * @returns true if reduction was successful.
	 */
	virtual bool ReduceSkeletalMesh(
		class USkeletalMesh* SkeletalMesh,
		int32 LODIndex,
		const struct FSkeletalMeshOptimizationSettings& Settings,
		bool bCalcLODDistance
		) = 0;
	/**
	 * Returns a unique string identifying both the reduction plugin itself and the version of the plugin.
	 */
	virtual const FString& GetVersionString() const = 0;

	/**
	 *	Returns true if mesh reduction is supported
	 */
	virtual bool IsSupported() const = 0;
};

//
namespace MaterialExportUtils
{
	struct FFlattenMaterial;
};

/**
 * Mesh merging interface.
 */
class IMeshMerging
{
public:
	virtual void BuildProxy(
		const TArray<FRawMesh>& InputMeshes,
		const TArray<MaterialExportUtils::FFlattenMaterial>& InputMaterials,
		const struct FMeshProxySettings& InProxySettings,
		FRawMesh& OutProxyMesh,
		MaterialExportUtils::FFlattenMaterial& OutMaterial
		) = 0;
};

/**
 * Mesh merging settings
 */
struct FMeshMergingSettings
{
	/** Whether to generate atlased lightmap UVs for a merged mesh*/
	bool bGenerateAtlasedLightMapUV;
	
	/** Target UV channel in a merged mesh for an atlased lightmap */
	int32 TargetLightMapUVChannel;

	/** Upper bounds for an atlased lightmap resolution */
	int32 MaxAltlasedLightMapResolution;
		
	/** Whether we should import vertex colors into merged mesh */
	bool bImportVertexColors;
	
	/** Whether merged mesh should have pivot at world origin, or at first merged component otherwise */
	bool bPivotPointAtZero;
		
	/** Default settings. */
	FMeshMergingSettings()
		: bGenerateAtlasedLightMapUV(true)
		, TargetLightMapUVChannel(1)
		, MaxAltlasedLightMapResolution(1024)
		, bImportVertexColors(false)
		, bPivotPointAtZero(false)
	{
	}
};



/**
 * Mesh reduction module interface.
 */
class IMeshReductionModule : public IModuleInterface
{
public:
	/**
	 * Retrieve the mesh reduction interface.
	 */
	virtual class IMeshReduction* GetMeshReductionInterface() = 0;
	
	/**
	 * Retrieve the mesh merging interface.
	 */
	virtual class IMeshMerging* GetMeshMergingInterface() = 0;
};

class IMeshUtilities : public IModuleInterface
{
public:
	/** Returns a string uniquely identifying this version of mesh utilities. */
	virtual const FString& GetVersionString() const = 0;

	/**
	 * Builds a renderable static mesh using the provided source models and the LOD groups settings.
	 * @returns true if the renderable mesh was built successfully.
	 */
	virtual bool BuildStaticMesh(
		class FStaticMeshRenderData& OutRenderData,
		TArray<struct FStaticMeshSourceModel>& SourceModels,
		const TArray<UMaterialInterface*>& Materials,
		const class FStaticMeshLODGroup& LODGroup
		) = 0;

	/**
	 * Create all render specific data for a skeletal mesh LOD model
	 * @returns true if the mesh was built successfully.
	 */
	virtual bool BuildSkeletalMesh( 
		FStaticLODModel& LODModel,
		const FReferenceSkeleton& RefSkeleton,
		const TArray<FVertInfluence>& Influences, 
		const TArray<FMeshWedge>& Wedges, 
		const TArray<FMeshFace>& Faces, 
		const TArray<FVector>& Points,
		const TArray<int32>& PointToOriginalMap,
		bool bKeepOverlappingVertices = false,
		bool bComputeNormals = true,
		bool bComputeTangents = true,
		TArray<FText> * OutWarningMessages = NULL,
		TArray<FName> * OutWarningNames = NULL
		) = 0;


	/**
	 * Generates a unique UV layout for a static mesh
	 * @param RawMesh - The input/output mesh
	 * @param TexCoordIndex - Index of the uv channel to overwrite or create
	 * @param bKeepExistingCoordinates - True to preserve the existing charts when packing
	 * @param MinChartSpacingPercent - Minimum distance between two packed charts (0.0-100.0)
	 * @param BorderSpacingPercent - Spacing between UV border and charts (0.0-100.0)
	 * @param bUseMaxStretch - True if "MaxDesiredStretch" should be used; otherwise "MaxCharts" is used
	 * @param InFalseEdgeIndices - Optional list of raw face edge indices to be ignored when creating UV seams
	 * @param MaxCharts - In: Max number of charts to allow; Out:Number of charts generated by the uv unwrap algorithm.
	 * @param MaxDesiredStretch - The amount of stretching allowed. 0 means no stretching is allowed, 1 means any amount of stretching can be used. 
	 * @param OutError - if unsuccessful, contains the error that occurred.
	 * @return true if successful
	 */
	virtual bool GenerateUVs(
		struct FRawMesh& RawMesh,
		uint32 TexCoordIndex,
		float MinChartSpacingPercent,
		float BorderSpacingPercent,
		bool bUseMaxStretch,
		const TArray< int32 >* InFalseEdgeIndices,
		uint32& MaxCharts,
		float& MaxDesiredStretch,
		FText& OutError
		) = 0;

	/**
	 * For quick generating lightmap uvs.
	 * It copies charts from 0 uv channel and layouts them without making new charts (keeps edge splits). Additionally separates folded triangles automatically
	 * Use when DXD generates ugly cuts and degenerates charts 
	 * @param RawMesh - The input/output mesh
	 * @param TexCoordIndex - Index of the uv channel to overwrite or create
	 * @param OutError - if unsuccessful, contains the error that occurred.
	 * @return true if successful
	 */
	virtual bool LayoutUVs(
	struct FRawMesh& RawMesh,
		uint32 TextureResolution,
		uint32 TexCoordIndex,
		FText& OutError
		) = 0;

	/** Returns the mesh reduction plugin if available. */
	virtual IMeshReduction* GetMeshReductionInterface() = 0;

	/** Returns the mesh merging plugin if available. */
	virtual IMeshMerging* GetMeshMergingInterface() = 0;

	/** Cache optimize the index buffer. */
	virtual void CacheOptimizeIndexBuffer(TArray<uint16>& Indices) = 0;

	/** Cache optimize the index buffer. */
	virtual void CacheOptimizeIndexBuffer(TArray<uint32>& Indices) = 0;

	/** Build adjacency information for the skeletal mesh used for tessellation. */
	virtual void BuildSkeletalAdjacencyIndexBuffer(
		const TArray<struct FSoftSkinVertex>& VertexBuffer,
		const uint32 TexCoordCount,
		const TArray<uint32>& Indices,
		TArray<uint32>& OutPnAenIndices
		) = 0;

	
	/**
	 *	Calculate the verts associated weighted to each bone of the skeleton.
	 *	The vertices returned are in the local space of the bone.
	 *
	 *	@param	SkeletalMesh	The target skeletal mesh.
	 *	@param	Infos			The output array of vertices associated with each bone.
	 *	@param	bOnlyDominant	Controls whether a vertex is added to the info for a bone if it is most controlled by that bone, or if that bone has ANY influence on that vert.
	 */
	virtual void CalcBoneVertInfos( USkeletalMesh* SkeletalMesh, TArray<FBoneVertInfo>& Infos, bool bOnlyDominant) = 0;

	/**
	 * Harvest static mesh components from input actors 
	 * and merge into signle mesh grouping them by unique materials
	 *
	 * @param SourceActors				List of actors to merge
	 * @param InSettings				Settings to use
	 * @param PackageName				Destination package name for a generated assets
	 * @param OutAssetsToSync			Merged mesh assets
	 * @param OutMergedActorLocation	World position of merged mesh
	 */
	virtual void MergeActors(
		const TArray<AActor*>& SourceActors,
		const FMeshMergingSettings& InSettings,
		const FString& PackageName, 
		TArray<UObject*>& OutAssetsToSync, 
		FVector& OutMergedActorLocation) const = 0;
	
	/**
	 *	Merges list of actors into single proxy mesh
	 *
	 *	@param	Actors					List of Actors to merge
	 *	@param	InProxySettings			Merge settings
	 *	@param	InOuter					Package for a generated assets, if NULL new packages will be created for each asset
	 *	@param	ProxyBasePackageName	Will be used for naming generated assets, in case InOuter is not specified ProxyBasePackageName will be used as long package name for creating new packages
	 *	@param	OutAssetsToSync			Result assets - mesh, material
	 *	@param	OutProxyLocation		Proxy mesh location in the world (bounding box origin of merged actors)
	 */
	virtual void CreateProxyMesh(
		const TArray<AActor*>& Actors,
		const struct FMeshProxySettings& InProxySettings,
		UPackage* InOuter,
		const FString& ProxyBasePackageName,
		TArray<UObject*>& OutAssetsToSync,
		FVector& OutProxyLocation
		) = 0;
};
