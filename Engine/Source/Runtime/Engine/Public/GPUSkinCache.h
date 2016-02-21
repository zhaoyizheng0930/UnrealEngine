// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.
// Copyright (C) Microsoft. All rights reserved.

/*=============================================================================
	GPUSkinCache.h: Performs skinning on a compute shader into a buffer to avoid vertex buffer skinning.
=============================================================================*/

#pragma once

#include "Array.h"
#include "RHIResources.h"
#include "RenderResource.h"
#include "UniformBuffer.h"

typedef FRHIShaderResourceView*              FShaderResourceViewRHIParamRef;

extern ENGINE_API int32 GEnableGPUSkinCacheShaders;
extern ENGINE_API int32 GEnableGPUSkinCache;

class FSkeletalMeshObjectGPUSkin;

class FGPUSkinCache : public FRenderResource
{
public:
	enum ESkinCacheInitSettings
	{
		// max 256 bones as we use a byte to index
		MaxUniformBufferBones = 256,
		// we allow up to this amount of objects being tracked
		MaxCachedElements = 1024,
		MaxCachedVertexBufferSRVs = 128,

		// Controls the output format on GpuSkinCacheComputeShader.usf
		RWPositionOffsetInFloats = 0,	// float3
		RWTangentXOffsetInFloats = 3,	// Packed U8x4N
		RWTangentZOffsetInFloats = 4,	// Packed U8x4N

		RWStrideInFloats = 5,
	};

	FGPUSkinCache();
	~FGPUSkinCache();

	// FRenderResource overrides
	virtual void ReleaseRHI() override;

	void Initialize(FRHICommandListImmediate& RHICmdList);
	void Cleanup();

	inline bool IsElementProcessed(int32 Key) const
	{
		if (!GEnableGPUSkinCache)
		{
			return false;
		}

		return InternalIsElementProcessed(Key);
	}

	// For each SkeletalMeshObject:
	//	Call Begin*
	//	For each Chunk:
	//		Call Add*
	//	Call End*
	// @param MorphVertexBuffer if no moprh targets are used
	// @returns -1 if failed, otherwise index into CachedElements[]
	int32 StartCacheMesh(FRHICommandListImmediate& RHICmdList, int32 Key, class FGPUBaseSkinVertexFactory* VertexFactory,
		class FGPUSkinPassthroughVertexFactory* TargetVertexFactory, const struct FSkelMeshChunk& BatchElement, const FSkeletalMeshObjectGPUSkin* Skin,
		const class FMorphVertexBuffer* MorphVertexBuffer);

	inline bool SetVertexStreamFromCache(FRHICommandList& RHICmdList, int32 Key, FShader* Shader, const FGPUSkinPassthroughVertexFactory* VertexFactory, uint32 BaseVertexIndex, FShaderParameter PreviousStreamFloatOffset, FShaderResourceParameter PreviousStreamBuffer)
	{
		if (Key >= 0 && Key < CachedElements.Num())
		{
			return InternalSetVertexStreamFromCache(RHICmdList, Key, Shader, VertexFactory, BaseVertexIndex, PreviousStreamFloatOffset, PreviousStreamBuffer);
		}

		return false;
	}

	struct FElementCacheStatusInfo
	{
		const FSkelMeshChunk* BatchElement;
		const FVertexFactory* VertexFactory;
		const FVertexFactory* TargetVertexFactory;
		const FSkeletalMeshObjectGPUSkin* Skin;

		int32	Key;
		// index into CachedVertexBuffers[] or -1 if it has no assignment yet
		int32	VertexBufferSRVIndex;
		int32	MorphVertexBufferSRVIndex;

		uint32	FrameUpdated;

		uint32	InputVBStride;
		// for non velocity rendering
		uint32	StreamOffset;
		// for velocity rendering (previous StreamOffset)
		uint32	PreviousFrameStreamOffset;

		bool	bExtraBoneInfluences;

		FElementCacheStatusInfo()
			: VertexBufferSRVIndex(-1)
			, MorphVertexBufferSRVIndex(-1)
		{
		}

		struct FSearchInfo
		{
			const FSkeletalMeshObjectGPUSkin* Skin;
			const FSkelMeshChunk* BatchElement;

			FSearchInfo()  { BatchElement = nullptr; Skin = nullptr; }
			FSearchInfo(const FSkeletalMeshObjectGPUSkin* InSkin, const FSkelMeshChunk* InBatchElement) { BatchElement = InBatchElement; Skin = InSkin; }
		};

		// to support FindByKey()
		bool operator == (const FSearchInfo& OtherSearchInfo) const 
		{
			return (BatchElement == OtherSearchInfo.BatchElement && Skin == OtherSearchInfo.Skin);
		}

		// to support == (confusing usage, better: IsSameBatchElement)
		bool operator == (const FSkelMeshChunk& OtherBatchElement) const 
		{
			return (BatchElement == &OtherBatchElement);
		}
	};

	ENGINE_API void TransitionToReadable(FRHICommandList& RHICmdList);
	ENGINE_API void TransitionToWriteable(FRHICommandList& RHICmdList);


private:
	FElementCacheStatusInfo* FindEvictableCacheStatusInfo();

	struct FVertexBufferInfo;

	// @param SkinType 0:normal, 1:with morph target, 2:with APEX cloth (not yet implemented)
	void DispatchSkinCacheProcess(FRHICommandListImmediate& RHICmdList, uint32 InputStreamFloatOffset, uint32 OutputBufferFloatOffset,
		const struct FVertexBufferAndSRV& BoneBuffer, FUniformBufferRHIRef UniformBuffer, const FVertexBufferInfo* VBInfo,
		uint32 VertexStride, uint32 VertexCount, const FVector& MeshOrigin, const FVector& MeshExtension, bool bUseExtraBoneInfluences, ERHIFeatureLevel::Type FeatureLevel,
		uint32 SkinType, uint32 InMorphInputStreamFloatOffset, FShaderResourceViewRHIParamRef MorphVertexBufferSRV);

	bool InternalSetVertexStreamFromCache(FRHICommandList& RHICmdList, int32 Key, FShader* Shader, const class FGPUSkinPassthroughVertexFactory* VertexFactory, uint32 BaseVertexIndex, FShaderParameter PreviousStreamFloatOffset, FShaderResourceParameter PreviousStreamBuffer);

	bool	bInitialized : 1;

	void TransitionAllToWriteable(FRHICommandList& RHICmdList);
	bool InternalIsElementProcessed(int32 Key) const;

	uint32	FrameCounter;

	int		CacheMaxVectorCount;
	int		CacheCurrentFloatOffset;
	int		CachedChunksThisFrameCount;

	TArray<FElementCacheStatusInfo>	CachedElements;

	FRWBuffer	SkinCacheBuffer[GPUSKINCACHE_FRAMES];

	static void CVarSinkFunction();

	static FAutoConsoleVariableSink CVarSink;

	
	// -------------------------------------------------
	
	// to cache the SRV needed to access the input vertex buffers
	struct FVertexBufferInfo
	{
		// key: input vertex buffer from which we need an SRV
		const FVertexBuffer* VertexBuffer;
		// value: SRV created from the VertexBuffer
		FShaderResourceViewRHIRef VertexBufferSRV;
		
		// to support FindByKey()
		bool operator == (const FVertexBuffer& OtherVertexBuffer) const 
		{
			return (VertexBuffer == &OtherVertexBuffer);
		}
	};
	
	// to cache the SRV needed to access the input vertex buffers
	TArray<FVertexBufferInfo> CachedVertexBuffers;
	
	FVertexBufferInfo* ReuseOrFindAndCacheSRV(int32& InOutIndex, const FVertexBuffer& VertexBuffer);
};

BEGIN_UNIFORM_BUFFER_STRUCT(GPUSkinCacheBonesUniformShaderParameters,)
	DECLARE_UNIFORM_BUFFER_STRUCT_MEMBER_ARRAY(FVector4,BoneVectors,[FGPUSkinCache::MaxUniformBufferBones*3])
END_UNIFORM_BUFFER_STRUCT(GPUSkinCacheBonesUniformShaderParameters)

extern ENGINE_API TGlobalResource<FGPUSkinCache> GGPUSkinCache;

DECLARE_STATS_GROUP(TEXT("GPU Skin Cache"), STATGROUP_GPUSkinCache, STATCAT_Advanced);

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Total Num Chunks"), STAT_GPUSkinCache_TotalNumChunks, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Total Num Vertices"), STAT_GPUSkinCache_TotalNumVertices, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Total Memory Used"), STAT_GPUSkinCache_TotalMemUsed, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Skipped for Max Chunks Per LOD"), STAT_GPUSkinCache_SkippedForMaxChunksPerLOD, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Skipped for Num Chunks Per Frame"), STAT_GPUSkinCache_SkippedForChunksPerFrame, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Skipped for zero influences"), STAT_GPUSkinCache_SkippedForZeroInfluences, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Skipped for Cloth"), STAT_GPUSkinCache_SkippedForCloth, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Skipped for Memory"), STAT_GPUSkinCache_SkippedForMemory, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Skipped for Already Cached"), STAT_GPUSkinCache_SkippedForAlreadyCached, STATGROUP_GPUSkinCache,);
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Skipped for out of ECS"), STAT_GPUSkinCache_SkippedForOutOfECS, STATGROUP_GPUSkinCache,);