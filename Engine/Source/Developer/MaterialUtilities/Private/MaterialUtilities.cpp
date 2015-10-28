// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
#include "MaterialUtilitiesPrivatePCH.h"

#include "Runtime/Engine/Classes/Engine/World.h"
#include "Runtime/Engine/Classes/Materials/MaterialInterface.h"
#include "Runtime/Engine/Classes/Materials/MaterialExpressionConstant.h"
#include "Runtime/Engine/Classes/Materials/MaterialExpressionConstant4Vector.h"
#include "Runtime/Engine/Classes/Materials/MaterialExpressionMultiply.h"
#include "Runtime/Engine/Classes/Engine/TextureRenderTarget2D.h"
#include "Runtime/Engine/Classes/Engine/Texture2D.h"
#include "Runtime/Engine/Classes/Engine/TextureCube.h"
#include "Runtime/Engine/Public/TileRendering.h"
#include "Runtime/Engine/Public/EngineModule.h"
#include "Runtime/Engine/Public/ImageUtils.h"
#include "Runtime/Engine/Public/CanvasTypes.h"
#include "Runtime/Engine/Public/MaterialCompiler.h"
#include "Runtime/Engine/Classes/Engine/TextureLODSettings.h"
#include "Runtime/Engine/Classes/DeviceProfiles/DeviceProfileManager.h"
#include "Runtime/Engine/Classes/Materials/MaterialParameterCollection.h" 
#include "RendererInterface.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "MeshUtilities.h"
#include "MeshRendering.h"
#include "MeshMergeData.h"

IMPLEMENT_MODULE(FMaterialUtilities, MaterialUtilities);

DEFINE_LOG_CATEGORY_STATIC(LogMaterialUtilities, Log, All);

bool FMaterialUtilities::CurrentlyRendering = false;
TArray<UTextureRenderTarget2D*> FMaterialUtilities::RenderTargetPool;

void FMaterialUtilities::StartupModule()
{
	FCoreUObjectDelegates::PreGarbageCollect.AddRaw(this, &FMaterialUtilities::OnPreGarbageCollect);
}

void FMaterialUtilities::ShutdownModule()
{
	FCoreUObjectDelegates::PreGarbageCollect.RemoveAll(this);
	ClearRenderTargetPool();
}

void FMaterialUtilities::OnPreGarbageCollect()
{
	ClearRenderTargetPool();
}


/*------------------------------------------------------------------------------
	Helper classes for render material to texture
------------------------------------------------------------------------------*/
struct FExportMaterialCompiler : public FProxyMaterialCompiler
{
	FExportMaterialCompiler(FMaterialCompiler* InCompiler) :
		FProxyMaterialCompiler(InCompiler)
	{}

	// gets value stored by SetMaterialProperty()
	virtual EShaderFrequency GetCurrentShaderFrequency() const override
	{
		// not used by Lightmass
		return SF_Pixel;
	}

	virtual int32 WorldPosition(EWorldPositionIncludedOffsets WorldPositionIncludedOffsets) override
	{
		return Compiler->WorldPosition(WorldPositionIncludedOffsets);
	}

	virtual int32 ObjectWorldPosition() override
	{
		return Compiler->Constant3(0.0f, 0.0f, 0.0f);
	}

	virtual int32 DistanceCullFade() override
	{
		return Compiler->Constant(1.0f);
	}

	virtual int32 ActorWorldPosition() override
	{
		return Compiler->Constant3(0.0f, 0.0f, 0.0f);
	}

	virtual int32 ParticleRelativeTime() override
	{
		return Compiler->Constant(0.0f);
	}

	virtual int32 ParticleMotionBlurFade() override
	{
		return Compiler->Constant(1.0f);
	}

	virtual int32 ParticleRandom() override
	{
		return Compiler->Constant(0.0f);
	}

	virtual int32 ParticleDirection() override
	{
		return Compiler->Constant3(0.0f, 0.0f, 0.0f);
	}

	virtual int32 ParticleSpeed() override
	{
		return Compiler->Constant(0.0f);
	}
	
	virtual int32 ParticleSize() override
	{
		return Compiler->Constant2(0.0f,0.0f);
	}

	virtual int32 ObjectRadius() override
	{
		return Compiler->Constant(500);
	}

	virtual int32 ObjectBounds() override
	{
		return Compiler->Constant3(0, 0, 0);
	}

	virtual int32 CameraVector() override
	{
		return Compiler->Constant3(0.0f, 0.0f, 1.0f);
	}
	
	virtual int32 ReflectionAboutCustomWorldNormal(int32 CustomWorldNormal, int32 bNormalizeCustomWorldNormal) override
	{
		return Compiler->Constant3(0.0f, 0.0f, -1.0f);
	}

	virtual int32 VertexColor() override
	{
		return Compiler->VertexColor(); 
	}

	virtual int32 LightVector() override
	{
		return Compiler->Constant3(1.0f, 0.0f, 0.0f);
	}

	virtual int32 ReflectionVector() override
	{
		return Compiler->Constant3(0.0f, 0.0f, -1.0f);
	}

	virtual int32 AtmosphericFogColor(int32 WorldPosition) override
	{
		return INDEX_NONE;
	}

	virtual int32 PrecomputedAOMask() override 
	{ 
		return Compiler->PrecomputedAOMask();
	}

	virtual int32 AccessCollectionParameter(UMaterialParameterCollection* ParameterCollection, int32 ParameterIndex, int32 ComponentIndex) override
	{
		if (!ParameterCollection || ParameterIndex == -1)
		{
			return INDEX_NONE;
		}

		// Collect names of all parameters
		TArray<FName> ParameterNames;
		ParameterCollection->GetParameterNames(ParameterNames, /*bVectorParameters=*/ false);
		int32 NumScalarParameters = ParameterNames.Num();
		ParameterCollection->GetParameterNames(ParameterNames, /*bVectorParameters=*/ true);

		// Find a parameter corresponding to ParameterIndex/ComponentIndex pair
		int32 Index;
		for (Index = 0; Index < ParameterNames.Num(); Index++)
		{
			FGuid ParameterId = ParameterCollection->GetParameterId(ParameterNames[Index]);
			int32 CheckParameterIndex, CheckComponentIndex;
			ParameterCollection->GetParameterIndex(ParameterId, CheckParameterIndex, CheckComponentIndex);
			if (CheckParameterIndex == ParameterIndex && CheckComponentIndex == ComponentIndex)
			{
				// Found
				break;
			}
		}
		if (Index >= ParameterNames.Num())
		{
			// Not found, should not happen
			return INDEX_NONE;
		}

		// Create code for parameter
		if (Index < NumScalarParameters)
		{
			const FCollectionScalarParameter* ScalarParameter = ParameterCollection->GetScalarParameterByName(ParameterNames[Index]);
			check(ScalarParameter);
			return Constant(ScalarParameter->DefaultValue);
		}
		else
		{
			const FCollectionVectorParameter* VectorParameter = ParameterCollection->GetVectorParameterByName(ParameterNames[Index]);
			check(VectorParameter);
			const FLinearColor& Color = VectorParameter->DefaultValue;
			return Constant4(Color.R, Color.G, Color.B, Color.A);
		}
	}
	
	virtual int32 LightmassReplace(int32 Realtime, int32 Lightmass) override { return Realtime; }
	virtual int32 MaterialProxyReplace(int32 Realtime, int32 MaterialProxy) override { return MaterialProxy; }
};


class FExportMaterialProxy : public FMaterial, public FMaterialRenderProxy
{
public:
	FExportMaterialProxy()
		: FMaterial()
	{
		SetQualityLevelProperties(EMaterialQualityLevel::High, false, GMaxRHIFeatureLevel);
	}

	FExportMaterialProxy(UMaterialInterface* InMaterialInterface, EMaterialProperty InPropertyToCompile)
		: FMaterial()
		, MaterialInterface(InMaterialInterface)
		, PropertyToCompile(InPropertyToCompile)
	{
		SetQualityLevelProperties(EMaterialQualityLevel::High, false, GMaxRHIFeatureLevel);
		Material = InMaterialInterface->GetMaterial();
		Material->AppendReferencedTextures(ReferencedTextures);
		FPlatformMisc::CreateGuid(Id);

		FMaterialResource* Resource = InMaterialInterface->GetMaterialResource(GMaxRHIFeatureLevel);

		FMaterialShaderMapId ResourceId;
		Resource->GetShaderMapId(GMaxRHIShaderPlatform, ResourceId);

		{
			TArray<FShaderType*> ShaderTypes;
			TArray<FVertexFactoryType*> VFTypes;
			GetDependentShaderAndVFTypes(GMaxRHIShaderPlatform, ShaderTypes, VFTypes);

			// Overwrite the shader map Id's dependencies with ones that came from the FMaterial actually being compiled (this)
			// This is necessary as we change FMaterial attributes like GetShadingModel(), which factor into the ShouldCache functions that determine dependent shader types
			ResourceId.SetShaderDependencies(ShaderTypes, VFTypes);
		}

		// Override with a special usage so we won't re-use the shader map used by the material for rendering
		switch (InPropertyToCompile)
		{
		case MP_BaseColor: ResourceId.Usage = EMaterialShaderMapUsage::MaterialExportBaseColor; break;
		case MP_Specular: ResourceId.Usage = EMaterialShaderMapUsage::MaterialExportSpecular; break;
		case MP_Normal: ResourceId.Usage = EMaterialShaderMapUsage::MaterialExportNormal; break;
		case MP_Metallic: ResourceId.Usage = EMaterialShaderMapUsage::MaterialExportMetallic; break;
		case MP_Roughness: ResourceId.Usage = EMaterialShaderMapUsage::MaterialExportRoughness; break;
		case MP_AmbientOcclusion: ResourceId.Usage = EMaterialShaderMapUsage::MaterialExportAO; break;
		};
		
		CacheShaders(ResourceId, GMaxRHIShaderPlatform, true);
	}

	/** This override is required otherwise the shaders aren't ready for use when the surface is rendered resulting in a blank image */
	virtual bool RequiresSynchronousCompilation() const override { return true; };

	/**
	* Should the shader for this material with the given platform, shader type and vertex 
	* factory type combination be compiled
	*
	* @param Platform		The platform currently being compiled for
	* @param ShaderType	Which shader is being compiled
	* @param VertexFactory	Which vertex factory is being compiled (can be NULL)
	*
	* @return true if the shader should be compiled
	*/
	virtual bool ShouldCache(EShaderPlatform Platform, const FShaderType* ShaderType, const FVertexFactoryType* VertexFactoryType) const override
	{
		// Always cache - decreases performance but avoids missing shaders during exports.
		return true;
	}

	virtual const TArray<UTexture*>& GetReferencedTextures() const override
	{
		return ReferencedTextures;
	}

	////////////////
	// FMaterialRenderProxy interface.
	virtual const FMaterial* GetMaterial(ERHIFeatureLevel::Type FeatureLevel) const override
	{
		if(GetRenderingThreadShaderMap())
		{
			return this;
		}
		else
		{
			return UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy(false)->GetMaterial(FeatureLevel);
		}
	}

	virtual bool GetVectorValue(const FName ParameterName, FLinearColor* OutValue, const FMaterialRenderContext& Context) const override
	{
		return MaterialInterface->GetRenderProxy(0)->GetVectorValue(ParameterName, OutValue, Context);
	}

	virtual bool GetScalarValue(const FName ParameterName, float* OutValue, const FMaterialRenderContext& Context) const override
	{
		return MaterialInterface->GetRenderProxy(0)->GetScalarValue(ParameterName, OutValue, Context);
	}

	virtual bool GetTextureValue(const FName ParameterName,const UTexture** OutValue, const FMaterialRenderContext& Context) const override
	{
		return MaterialInterface->GetRenderProxy(0)->GetTextureValue(ParameterName,OutValue,Context);
	}

	// Material properties.
	/** Entry point for compiling a specific material property.  This must call SetMaterialProperty. */
	virtual int32 CompilePropertyAndSetMaterialProperty(EMaterialProperty Property, FMaterialCompiler* Compiler, EShaderFrequency OverrideShaderFrequency, bool bUsePreviousFrameTime) const override
	{
		// needs to be called in this function!!
		Compiler->SetMaterialProperty(Property, OverrideShaderFrequency, bUsePreviousFrameTime);

		int32 Ret = CompilePropertyAndSetMaterialPropertyWithoutCast(Property, Compiler);

		return Compiler->ForceCast(Ret, GetMaterialPropertyType(Property));
	}

	/** helper for CompilePropertyAndSetMaterialProperty() */
	int32 CompilePropertyAndSetMaterialPropertyWithoutCast(EMaterialProperty Property, FMaterialCompiler* Compiler) const
	{
		/*TScopedPointer<FMaterialCompiler> CompilerProxyHolder;
		if (CompilerReplacer != nullptr)
		{
			CompilerProxyHolder = CompilerReplacer(Compiler);
			Compiler = CompilerProxyHolder;
		}*/

		if (Property == MP_EmissiveColor)
		{
			UMaterial* ProxyMaterial = MaterialInterface->GetMaterial();
			check(ProxyMaterial);
			EBlendMode BlendMode = MaterialInterface->GetBlendMode();
			EMaterialShadingModel ShadingModel = MaterialInterface->GetShadingModel();
			FExportMaterialCompiler ProxyCompiler(Compiler);
									
			switch (PropertyToCompile)
			{
			case MP_EmissiveColor:
				// Emissive is ALWAYS returned...
				return Compiler->ForceCast(MaterialInterface->CompileProperty(&ProxyCompiler,MP_EmissiveColor),MCT_Float3,true,true);
			case MP_BaseColor:
				// Only return for Opaque and Masked...
				if (BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked)
				{
					return Compiler->ForceCast(MaterialInterface->CompileProperty(&ProxyCompiler, MP_BaseColor),MCT_Float3,true,true);
				}
				break;
			case MP_Specular: 
			case MP_Roughness:
			case MP_Metallic:
			case MP_AmbientOcclusion:
				// Only return for Opaque and Masked...
				if (BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked)
				{
					return Compiler->ForceCast(MaterialInterface->CompileProperty(&ProxyCompiler, PropertyToCompile),MCT_Float,true,true);
				}
				break;
			case MP_Normal:
				// Only return for Opaque and Masked...
				if (BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked)
				{
					return Compiler->ForceCast( 
						Compiler->Add( 
							Compiler->Mul(MaterialInterface->CompileProperty(&ProxyCompiler, MP_Normal), Compiler->Constant(0.5f)), // [-1,1] * 0.5
							Compiler->Constant(0.5f)), // [-0.5,0.5] + 0.5
						MCT_Float3, true, true );
				}
				break;
			default:
				return Compiler->Constant(1.0f);
			}
	
			return Compiler->Constant(0.0f);
		}
		else if (Property == MP_WorldPositionOffset)
		{
			//This property MUST return 0 as a default or during the process of rendering textures out for lightmass to use, pixels will be off by 1.
			return Compiler->Constant(0.0f);
		}
		else if (Property >= MP_CustomizedUVs0 && Property <= MP_CustomizedUVs7)
		{
			// Pass through customized UVs
			return MaterialInterface->CompileProperty(Compiler, Property);
		}
		else
		{
			return Compiler->Constant(1.0f);
		}
	}

	virtual FString GetMaterialUsageDescription() const override
	{
		return FString::Printf(TEXT("FExportMaterialRenderer %s"), MaterialInterface ? *MaterialInterface->GetName() : TEXT("NULL"));
	}
	virtual int32 GetMaterialDomain() const override
	{
		if (Material)
		{
			return Material->MaterialDomain;
		}
		return MD_Surface;
	}
	virtual bool IsTwoSided() const  override
	{ 
		if (MaterialInterface)
		{
			return MaterialInterface->IsTwoSided();
		}
		return false;
	}
	virtual bool IsDitheredLODTransition() const  override
	{ 
		if (MaterialInterface)
		{
			return MaterialInterface->IsDitheredLODTransition();
		}
		return false;
	}
	virtual bool IsLightFunction() const override
	{
		if (Material)
		{
			return (Material->MaterialDomain == MD_LightFunction);
		}
		return false;
	}
	virtual bool IsUsedWithDeferredDecal() const override
	{
		return	Material &&
				Material->MaterialDomain == MD_DeferredDecal;
	}
	virtual bool IsSpecialEngineMaterial() const override
	{
		if (Material)
		{
			return (Material->bUsedAsSpecialEngineMaterial == 1);
		}
		return false;
	}
	virtual bool IsWireframe() const override
	{
		if (Material)
		{
			return (Material->Wireframe == 1);
		}
		return false;
	}
	virtual bool IsMasked() const override								{ return false; }
	virtual enum EBlendMode GetBlendMode() const override				{ return BLEND_Opaque; }
	virtual enum EMaterialShadingModel GetShadingModel() const override	{ return MSM_Unlit; }
	virtual float GetOpacityMaskClipValue() const override				{ return 0.5f; }
	virtual FString GetFriendlyName() const override { return FString::Printf(TEXT("FExportMaterialRenderer %s"), MaterialInterface ? *MaterialInterface->GetName() : TEXT("NULL")); }
	/**
	* Should shaders compiled for this material be saved to disk?
	*/
	virtual bool IsPersistent() const override { return false; }
	virtual FGuid GetMaterialId() const override { return Id; }

	const UMaterialInterface* GetMaterialInterface() const
	{
		return MaterialInterface;
	}

	friend FArchive& operator<< ( FArchive& Ar, FExportMaterialProxy& V )
	{
		return Ar << V.MaterialInterface;
	}

	/**
	* Iterate through all textures used by the material and return the maximum texture resolution used
	* (ideally this could be made dependent of the material property)
	*
	* @param MaterialInterface The material to scan for texture size
	*
	* @return Size (width and height)
	*/
	FIntPoint FindMaxTextureSize(UMaterialInterface* InMaterialInterface, FIntPoint MinimumSize = FIntPoint(1, 1)) const
	{
		// static lod settings so that we only initialize them once
		UTextureLODSettings* GameTextureLODSettings = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings();

		TArray<UTexture*> MaterialTextures;

		InMaterialInterface->GetUsedTextures(MaterialTextures, EMaterialQualityLevel::Num, false, GMaxRHIFeatureLevel, false);

		// find the largest texture in the list (applying it's LOD bias)
		FIntPoint MaxSize = MinimumSize;
		for (int32 TexIndex = 0; TexIndex < MaterialTextures.Num(); TexIndex++)
		{
			UTexture* Texture = MaterialTextures[TexIndex];

			if (Texture == NULL)
			{
				continue;
			}

			// get the max size of the texture
			FIntPoint LocalSize(0, 0);
			if (Texture->IsA(UTexture2D::StaticClass()))
			{
				UTexture2D* Tex2D = (UTexture2D*)Texture;
				LocalSize = FIntPoint(Tex2D->GetSizeX(), Tex2D->GetSizeY());
			}
			else if (Texture->IsA(UTextureCube::StaticClass()))
			{
				UTextureCube* TexCube = (UTextureCube*)Texture;
				LocalSize = FIntPoint(TexCube->GetSizeX(), TexCube->GetSizeY());
			}

			int32 LocalBias = GameTextureLODSettings->CalculateLODBias(Texture);

			// bias the texture size based on LOD group
			FIntPoint BiasedLocalSize(LocalSize.X >> LocalBias, LocalSize.Y >> LocalBias);

			MaxSize.X = FMath::Max(BiasedLocalSize.X, MaxSize.X);
			MaxSize.Y = FMath::Max(BiasedLocalSize.Y, MaxSize.Y);
		}

		return MaxSize;
	}

	static bool WillFillData(EBlendMode InBlendMode, EMaterialProperty InMaterialProperty)
	{
		if (InMaterialProperty == MP_EmissiveColor)
		{
			return true;
		}

		switch (InBlendMode)
		{
		case BLEND_Opaque:
			{
				switch (InMaterialProperty)
				{
				case MP_BaseColor:		return true;
				case MP_Specular:		return true;
				case MP_Normal:			return true;
				case MP_Metallic:		return true;
				case MP_Roughness:		return true;
				case MP_AmbientOcclusion:		return true;
				}
			}
			break;
		}
		return false;
	}

private:
	/** The material interface for this proxy */
	UMaterialInterface* MaterialInterface;
	UMaterial* Material;	
	TArray<UTexture*> ReferencedTextures;
	/** The property to compile for rendering the sample */
	EMaterialProperty PropertyToCompile;
	FGuid Id;
};

static void RenderSceneToTexture(
		FSceneInterface* Scene,
		const FName& VisualizationMode, 
		const FVector& ViewOrigin,
		const FMatrix& ViewRotationMatrix, 
		const FMatrix& ProjectionMatrix,  
		const TSet<FPrimitiveComponentId>& HiddenPrimitives, 
		FIntPoint TargetSize,
		float TargetGamma,
		TArray<FColor>& OutSamples)
{
	auto RenderTargetTexture = NewObject<UTextureRenderTarget2D>();
	check(RenderTargetTexture);
	RenderTargetTexture->AddToRoot();
	RenderTargetTexture->ClearColor = FLinearColor::Transparent;
	RenderTargetTexture->TargetGamma = TargetGamma;
	RenderTargetTexture->InitCustomFormat(TargetSize.X, TargetSize.Y, PF_FloatRGBA, false);
	FTextureRenderTargetResource* RenderTargetResource = RenderTargetTexture->GameThread_GetRenderTargetResource();

	FSceneViewFamilyContext ViewFamily(
		FSceneViewFamily::ConstructionValues(RenderTargetResource, Scene, FEngineShowFlags(ESFIM_Game))
			.SetWorldTimes(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime)
		);

	// To enable visualization mode
	ViewFamily.EngineShowFlags.SetPostProcessing(true);
	ViewFamily.EngineShowFlags.SetVisualizeBuffer(true);
	ViewFamily.EngineShowFlags.SetTonemapper(false);

	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.SetViewRectangle(FIntRect(0, 0, TargetSize.X, TargetSize.Y));
	ViewInitOptions.ViewFamily = &ViewFamily;
	ViewInitOptions.HiddenPrimitives = HiddenPrimitives;
	ViewInitOptions.ViewOrigin = ViewOrigin;
	ViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
	ViewInitOptions.ProjectionMatrix = ProjectionMatrix;
		
	FSceneView* NewView = new FSceneView(ViewInitOptions);
	NewView->CurrentBufferVisualizationMode = VisualizationMode;
	ViewFamily.Views.Add(NewView);
					
	FCanvas Canvas(RenderTargetResource, NULL, FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime, Scene->GetFeatureLevel());
	Canvas.Clear(FLinearColor::Transparent);
	GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);

	// Copy the contents of the remote texture to system memory
	OutSamples.SetNumUninitialized(TargetSize.X*TargetSize.Y);
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	RenderTargetResource->ReadPixelsPtr(OutSamples.GetData(), ReadSurfaceDataFlags, FIntRect(0, 0, TargetSize.X, TargetSize.Y));
	FlushRenderingCommands();
					
	RenderTargetTexture->RemoveFromRoot();
	RenderTargetTexture = nullptr;
}



bool FMaterialUtilities::SupportsExport(EBlendMode InBlendMode, EMaterialProperty InMaterialProperty)
{
	return FExportMaterialProxy::WillFillData(InBlendMode, InMaterialProperty);
}

bool FMaterialUtilities::ExportMaterialProperty(UWorld* InWorld, UMaterialInterface* InMaterial, EMaterialProperty InMaterialProperty, UTextureRenderTarget2D* InRenderTarget, TArray<FColor>& OutBMP)
{
	TScopedPointer<FExportMaterialProxy> MaterialProxy(new FExportMaterialProxy(InMaterial, InMaterialProperty));
	if (MaterialProxy == NULL)
	{
		return false;
	}

	FBox2D DummyBounds(FVector2D(0, 0), FVector2D(1, 1));
	TArray<FVector2D> EmptyTexCoords;
	FMaterialMergeData MaterialData(InMaterial, nullptr, nullptr, 0, DummyBounds, EmptyTexCoords);
	const bool bForceGamma = (InMaterialProperty == MP_Normal) || (InMaterialProperty == MP_OpacityMask) || (InMaterialProperty == MP_Opacity);	

	FIntPoint MaxSize = MaterialProxy->FindMaxTextureSize(InMaterial);
	return RenderMaterialPropertyToTexture(MaterialData, InMaterialProperty, bForceGamma, PF_B8G8R8A8, MaxSize, OutBMP);
}

bool FMaterialUtilities::ExportMaterialProperty(UWorld* InWorld, UMaterialInterface* InMaterial, EMaterialProperty InMaterialProperty, FIntPoint& OutSize, TArray<FColor>& OutBMP)
{
	TScopedPointer<FExportMaterialProxy> MaterialProxy(new FExportMaterialProxy(InMaterial, InMaterialProperty));
	if (MaterialProxy == NULL)
	{
		return false;
	}

	FBox2D DummyBounds(FVector2D(0, 0), FVector2D(1, 1));
	TArray<FVector2D> EmptyTexCoords;
	FMaterialMergeData MaterialData(InMaterial, nullptr, nullptr, 0, DummyBounds, EmptyTexCoords);
	const bool bForceGamma = (InMaterialProperty == MP_Normal) || (InMaterialProperty == MP_OpacityMask) || (InMaterialProperty == MP_Opacity);
	OutSize = MaterialProxy->FindMaxTextureSize(InMaterial);
	return RenderMaterialPropertyToTexture(MaterialData, InMaterialProperty, bForceGamma, PF_B8G8R8A8, OutSize, OutBMP);
}

bool FMaterialUtilities::ExportMaterialProperty(UMaterialInterface* InMaterial, EMaterialProperty InMaterialProperty, TArray<FColor>& OutBMP, FIntPoint& OutSize)
{
	TScopedPointer<FExportMaterialProxy> MaterialProxy(new FExportMaterialProxy(InMaterial, InMaterialProperty));
	if (MaterialProxy == NULL)
	{
		return false;
	}

	FBox2D DummyBounds(FVector2D(0, 0), FVector2D(1, 1));
	TArray<FVector2D> EmptyTexCoords;
	FMaterialMergeData MaterialData(InMaterial, nullptr, nullptr, 0, DummyBounds, EmptyTexCoords);
	const bool bForceGamma = (InMaterialProperty == MP_Normal) || (InMaterialProperty == MP_OpacityMask) || (InMaterialProperty == MP_Opacity);
	OutSize = MaterialProxy->FindMaxTextureSize(InMaterial);
	return RenderMaterialPropertyToTexture(MaterialData, InMaterialProperty, bForceGamma, PF_B8G8R8A8, OutSize, OutBMP);
}

bool FMaterialUtilities::ExportMaterialProperty(UMaterialInterface* InMaterial, EMaterialProperty InMaterialProperty, FIntPoint InSize, TArray<FColor>& OutBMP)
{
	TScopedPointer<FExportMaterialProxy> MaterialProxy(new FExportMaterialProxy(InMaterial, InMaterialProperty));
	if (MaterialProxy == NULL)
	{
		return false;
	}

	FBox2D DummyBounds(FVector2D(0, 0), FVector2D(1, 1));
	TArray<FVector2D> EmptyTexCoords;
	FMaterialMergeData MaterialData(InMaterial, nullptr, nullptr, 0, DummyBounds, EmptyTexCoords);
	const bool bForceGamma = (InMaterialProperty == MP_Normal) || (InMaterialProperty == MP_OpacityMask) || (InMaterialProperty == MP_Opacity);
	return RenderMaterialPropertyToTexture(MaterialData, InMaterialProperty, bForceGamma, PF_B8G8R8A8, InSize, OutBMP);
}

bool FMaterialUtilities::ExportMaterial(UWorld* InWorld, UMaterialInterface* InMaterial, FFlattenMaterial& OutFlattenMaterial)
{
	return ExportMaterial(InMaterial, OutFlattenMaterial);
}

bool FMaterialUtilities::ExportMaterial(UMaterialInterface* InMaterial, FFlattenMaterial& OutFlattenMaterial, struct FExportMaterialProxyCache* ProxyCache)
{
	FBox2D DummyBounds(FVector2D(0, 0), FVector2D(1, 1));
	TArray<FVector2D> EmptyTexCoords;

	FMaterialMergeData MaterialData(InMaterial, nullptr, nullptr, 0, DummyBounds, EmptyTexCoords);
	ExportMaterial(MaterialData, OutFlattenMaterial, ProxyCache);
	return true;
}

bool FMaterialUtilities::ExportMaterial(UMaterialInterface* InMaterial, const FRawMesh* InMesh, int32 InMaterialIndex, const FBox2D& InTexcoordBounds, const TArray<FVector2D>& InTexCoords, FFlattenMaterial& OutFlattenMaterial, struct FExportMaterialProxyCache* ProxyCache)
{
	FMaterialMergeData MaterialData(InMaterial, InMesh, nullptr, InMaterialIndex, InTexcoordBounds, InTexCoords);
	ExportMaterial(MaterialData, OutFlattenMaterial, ProxyCache);
	return true;
}

bool FMaterialUtilities::ExportLandscapeMaterial(ALandscapeProxy* InLandscape, const TSet<FPrimitiveComponentId>& HiddenPrimitives, FFlattenMaterial& OutFlattenMaterial)
{
	check(InLandscape);

	FIntRect LandscapeRect = InLandscape->GetBoundingRect();
	FVector MidPoint = FVector(LandscapeRect.Min, 0.f) + FVector(LandscapeRect.Size(), 0.f)*0.5f;
		
	FVector LandscapeCenter = InLandscape->GetTransform().TransformPosition(MidPoint);
	FVector LandscapeExtent = FVector(LandscapeRect.Size(), 0.f)*InLandscape->GetActorScale()*0.5f; 

	FVector ViewOrigin = LandscapeCenter;
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(InLandscape->GetActorRotation());
	ViewRotationMatrix*= FMatrix(FPlane(1,	0,	0,	0),
							FPlane(0,	-1,	0,	0),
							FPlane(0,	0,	-1,	0),
							FPlane(0,	0,	0,	1));
				
	const float ZOffset = WORLD_MAX;
	FMatrix ProjectionMatrix =  FReversedZOrthoMatrix(
		LandscapeExtent.X,
		LandscapeExtent.Y,
		0.5f / ZOffset,
		ZOffset);

	FSceneInterface* Scene = InLandscape->GetWorld()->Scene;
						
	// Render diffuse texture using BufferVisualizationMode=BaseColor
	if (OutFlattenMaterial.DiffuseSize.X > 0 && 
		OutFlattenMaterial.DiffuseSize.Y > 0)
	{
		static const FName BaseColorName("BaseColor");
		const float BaseColorGamma = 2.2f; // BaseColor to gamma space
		RenderSceneToTexture(Scene, BaseColorName, ViewOrigin, ViewRotationMatrix, ProjectionMatrix, HiddenPrimitives, 
			OutFlattenMaterial.DiffuseSize, BaseColorGamma, OutFlattenMaterial.DiffuseSamples);
	}

	// Render normal map using BufferVisualizationMode=WorldNormal
	// Final material should use world space instead of tangent space for normals
	if (OutFlattenMaterial.NormalSize.X > 0 && 
		OutFlattenMaterial.NormalSize.Y > 0)
	{
		static const FName WorldNormalName("WorldNormal");
		const float NormalColorGamma = 1.0f; // Dump normal texture in linear space
		RenderSceneToTexture(Scene, WorldNormalName, ViewOrigin, ViewRotationMatrix, ProjectionMatrix, HiddenPrimitives, 
			OutFlattenMaterial.NormalSize, NormalColorGamma, OutFlattenMaterial.NormalSamples);
	}

	// Render metallic map using BufferVisualizationMode=Metallic
	if (OutFlattenMaterial.MetallicSize.X > 0 && 
		OutFlattenMaterial.MetallicSize.Y > 0)
	{
		static const FName MetallicName("Metallic");
		const float MetallicColorGamma = 1.0f; // Dump metallic texture in linear space
		RenderSceneToTexture(Scene, MetallicName, ViewOrigin, ViewRotationMatrix, ProjectionMatrix, HiddenPrimitives, 
			OutFlattenMaterial.MetallicSize, MetallicColorGamma, OutFlattenMaterial.MetallicSamples);
	}

	// Render roughness map using BufferVisualizationMode=Roughness
	if (OutFlattenMaterial.RoughnessSize.X > 0 && 
		OutFlattenMaterial.RoughnessSize.Y > 0)
	{
		static const FName RoughnessName("Roughness");
		const float RoughnessColorGamma = 2.2f; // Roughness material powers color by 2.2, transform it back to linear
		RenderSceneToTexture(Scene, RoughnessName, ViewOrigin, ViewRotationMatrix, ProjectionMatrix, HiddenPrimitives, 
			OutFlattenMaterial.RoughnessSize, RoughnessColorGamma, OutFlattenMaterial.RoughnessSamples);
	}

	// Render specular map using BufferVisualizationMode=Specular
	if (OutFlattenMaterial.SpecularSize.X > 0 && 
		OutFlattenMaterial.SpecularSize.Y > 0)
	{
		static const FName SpecularName("Specular");
		const float SpecularColorGamma = 1.0f; // Dump specular texture in linear space
		RenderSceneToTexture(Scene, SpecularName, ViewOrigin, ViewRotationMatrix, ProjectionMatrix, HiddenPrimitives, 
			OutFlattenMaterial.SpecularSize, SpecularColorGamma, OutFlattenMaterial.SpecularSamples);
	}
				
	OutFlattenMaterial.MaterialId = InLandscape->GetLandscapeGuid();
	return true;
}

UMaterial* FMaterialUtilities::CreateMaterial(const FFlattenMaterial& InFlattenMaterial, UPackage* InOuter, const FString& BaseName, EObjectFlags Flags, TArray<UObject*>& OutGeneratedAssets)
{
	// Base name for a new assets
	// In case outer is null BaseName has to be long package name
	if (InOuter == nullptr && FPackageName::IsShortPackageName(BaseName))
	{
		UE_LOG(LogMaterialUtilities, Warning, TEXT("Invalid long package name: '%s'."), *BaseName);
		return nullptr;
	}

	const FString AssetBaseName = FPackageName::GetShortName(BaseName);
	const FString AssetBasePath = InOuter ? TEXT("") : FPackageName::GetLongPackagePath(BaseName) + TEXT("/");
				
	// Create material
	const FString MaterialAssetName = TEXT("M_") + AssetBaseName;
	UPackage* MaterialOuter = InOuter;
	if (MaterialOuter == NULL)
	{
		MaterialOuter = CreatePackage(NULL, *(AssetBasePath + MaterialAssetName));
		MaterialOuter->FullyLoad();
		MaterialOuter->Modify();
	}

	UMaterial* Material = NewObject<UMaterial>(MaterialOuter, FName(*MaterialAssetName), Flags);
	Material->TwoSided = false;
	Material->SetShadingModel(MSM_DefaultLit);
	OutGeneratedAssets.Add(Material);

	int32 MaterialNodeY = -150;
	int32 MaterialNodeStepY = 180;

	// BaseColor
	if (InFlattenMaterial.DiffuseSamples.Num() > 1)
	{
		const FString AssetName = TEXT("T_") + AssetBaseName + TEXT("_D");
		const FString AssetLongName = AssetBasePath + AssetName;
		const bool bSRGB = true;
		UTexture2D* Texture = CreateTexture(InOuter, AssetLongName, InFlattenMaterial.DiffuseSize, InFlattenMaterial.DiffuseSamples, TC_Default, TEXTUREGROUP_World, Flags, bSRGB);
		OutGeneratedAssets.Add(Texture);
			
		auto BasecolorExpression = NewObject<UMaterialExpressionTextureSample>(Material);
		BasecolorExpression->Texture = Texture;
		BasecolorExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Color;
		BasecolorExpression->MaterialExpressionEditorX = -400;
		BasecolorExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(BasecolorExpression);
		Material->BaseColor.Expression = BasecolorExpression;

		MaterialNodeY+= MaterialNodeStepY;
	}
	else if (InFlattenMaterial.DiffuseSamples.Num() == 1)
	{
		// Set Roughness to constant
		FColor BaseColor = InFlattenMaterial.DiffuseSamples[0];
		auto BaseColorExpression = NewObject<UMaterialExpressionConstant4Vector>(Material);
		BaseColorExpression->Constant = BaseColor.ReinterpretAsLinear();
		BaseColorExpression->MaterialExpressionEditorX = -400;
		BaseColorExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(BaseColorExpression);
		Material->BaseColor.Expression = BaseColorExpression;

		MaterialNodeY += MaterialNodeStepY;
	}


	// Whether or not a material property is baked down
	const bool bHasMetallic = (InFlattenMaterial.MetallicSamples.Num() > 1);
	const bool bHasRoughness = (InFlattenMaterial.RoughnessSamples.Num() > 1);
	const bool bHasSpecular = (InFlattenMaterial.SpecularSamples.Num() > 1);

	// Number of material properties baked down to textures
	const int BakedMaterialPropertyCount = bHasMetallic + bHasRoughness + bHasSpecular;

	// Check for same texture sizes
	bool bSameTextureSize = true;	
	bSameTextureSize &= bHasMetallic ? (InFlattenMaterial.DiffuseSamples.Num() == InFlattenMaterial.MetallicSamples.Num()) : true;
	bSameTextureSize &= bHasRoughness ? (InFlattenMaterial.DiffuseSamples.Num() == InFlattenMaterial.RoughnessSamples.Num()) : true;
	bSameTextureSize &= bHasSpecular ? (InFlattenMaterial.DiffuseSamples.Num() == InFlattenMaterial.SpecularSamples.Num()) : true;
	// Merge values into one texture if more than one material property exists
	if (BakedMaterialPropertyCount > 1 && bSameTextureSize)
	{
		// Metallic = R, Roughness = G, Specular = B
		TArray<FColor> MergedSamples;
		const int32 SampleCount = InFlattenMaterial.DiffuseSamples.Num();
		MergedSamples.AddZeroed(SampleCount);
		
		if (bHasMetallic) for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			MergedSamples[SampleIndex].R = InFlattenMaterial.MetallicSamples[SampleIndex].R;
		}

		if (bHasRoughness) for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			MergedSamples[SampleIndex].G = InFlattenMaterial.RoughnessSamples[SampleIndex].G;
		}

		if (bHasSpecular) for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			MergedSamples[SampleIndex].B = InFlattenMaterial.SpecularSamples[SampleIndex].B;
		}

		const FString AssetName = TEXT("T_") + AssetBaseName + TEXT("_MRS");
		const bool bSRGB = false;
		UTexture2D* Texture = CreateTexture(InOuter, AssetBasePath + AssetName, InFlattenMaterial.DiffuseSize, MergedSamples, TC_Default, TEXTUREGROUP_World, Flags, bSRGB);
		OutGeneratedAssets.Add(Texture);

		auto MergedExpression = NewObject<UMaterialExpressionTextureSample>(Material);
		MergedExpression->Texture = Texture;
		MergedExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_LinearColor;
		MergedExpression->MaterialExpressionEditorX = -400;
		MergedExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(MergedExpression);

		// Metallic
		if (bHasMetallic)
		{
			Material->Metallic.Expression = MergedExpression;
			Material->Metallic.Mask = Material->Metallic.Expression->GetOutputs()[0].Mask;
			Material->Metallic.MaskR = 1;
			Material->Metallic.MaskG = 0;
			Material->Metallic.MaskB = 0;
			Material->Metallic.MaskA = 0;
		}

		// Roughness
		if (bHasRoughness)
		{
			Material->Roughness.Expression = MergedExpression;
			Material->Roughness.Mask = Material->Roughness.Expression->GetOutputs()[0].Mask;
			Material->Roughness.MaskR = 0;
			Material->Roughness.MaskG = 1;
			Material->Roughness.MaskB = 0;
			Material->Roughness.MaskA = 0;
		}
		
		// Specular
		if (bHasSpecular)
		{
			Material->Specular.Expression = MergedExpression;
			Material->Specular.Mask = Material->Specular.Expression->GetOutputs()[0].Mask;
			Material->Specular.MaskR = 0;
			Material->Specular.MaskG = 0;
			Material->Specular.MaskB = 1;
			Material->Specular.MaskA = 0;
		}

		MaterialNodeY += MaterialNodeStepY;
	}
	else
	{
		// Metallic
		if (InFlattenMaterial.MetallicSamples.Num() > 1)
		{
			const FString AssetName = TEXT("T_") + AssetBaseName + TEXT("_M");
			const bool bSRGB = false;
			UTexture2D* Texture = CreateTexture(InOuter, AssetBasePath + AssetName, InFlattenMaterial.MetallicSize, InFlattenMaterial.MetallicSamples, TC_Grayscale, TEXTUREGROUP_World, Flags, bSRGB);
			OutGeneratedAssets.Add(Texture);

			auto MetallicExpression = NewObject<UMaterialExpressionTextureSample>(Material);
			MetallicExpression->Texture = Texture;
			MetallicExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_LinearGrayscale;
			MetallicExpression->MaterialExpressionEditorX = -400;
			MetallicExpression->MaterialExpressionEditorY = MaterialNodeY;
			Material->Expressions.Add(MetallicExpression);
			Material->Metallic.Expression = MetallicExpression;

			MaterialNodeY += MaterialNodeStepY;
		}

		// Specular
		if (InFlattenMaterial.SpecularSamples.Num() > 1)
		{
			const FString AssetName = TEXT("T_") + AssetBaseName + TEXT("_S");
			const bool bSRGB = false;
			UTexture2D* Texture = CreateTexture(InOuter, AssetBasePath + AssetName, InFlattenMaterial.SpecularSize, InFlattenMaterial.SpecularSamples, TC_Grayscale, TEXTUREGROUP_World, Flags, bSRGB);
			OutGeneratedAssets.Add(Texture);

			auto SpecularExpression = NewObject<UMaterialExpressionTextureSample>(Material);
			SpecularExpression->Texture = Texture;
			SpecularExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_LinearGrayscale;
			SpecularExpression->MaterialExpressionEditorX = -400;
			SpecularExpression->MaterialExpressionEditorY = MaterialNodeY;
			Material->Expressions.Add(SpecularExpression);
			Material->Specular.Expression = SpecularExpression;

			MaterialNodeY += MaterialNodeStepY;
		}

		// Roughness
		if (InFlattenMaterial.RoughnessSamples.Num() > 1)
		{
			const FString AssetName = TEXT("T_") + AssetBaseName + TEXT("_R");
			const bool bSRGB = false;
			UTexture2D* Texture = CreateTexture(InOuter, AssetBasePath + AssetName, InFlattenMaterial.RoughnessSize, InFlattenMaterial.RoughnessSamples, TC_Grayscale, TEXTUREGROUP_World, Flags, bSRGB);
			OutGeneratedAssets.Add(Texture);

			auto RoughnessExpression = NewObject<UMaterialExpressionTextureSample>(Material);
			RoughnessExpression->Texture = Texture;
			RoughnessExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_LinearGrayscale;
			RoughnessExpression->MaterialExpressionEditorX = -400;
			RoughnessExpression->MaterialExpressionEditorY = MaterialNodeY;
			Material->Expressions.Add(RoughnessExpression);
			Material->Roughness.Expression = RoughnessExpression;

			MaterialNodeY += MaterialNodeStepY;
		}
	}

	if (InFlattenMaterial.MetallicSamples.Num() == 1)
	{
		// Set Metallic to constant
		float Metallic = *(float*)(&InFlattenMaterial.MetallicSamples[0].DWColor());
		auto MetallicExpression = NewObject<UMaterialExpressionConstant>(Material);
		MetallicExpression->R = Metallic;
		MetallicExpression->MaterialExpressionEditorX = -400;
		MetallicExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(MetallicExpression);
		Material->Metallic.Expression = MetallicExpression;

		MaterialNodeY += MaterialNodeStepY;
	}

	if (InFlattenMaterial.SpecularSamples.Num() == 1)
	{
		// Set Specular to constant
		float Specular = *(float*)(&InFlattenMaterial.SpecularSamples[0].DWColor());
		auto SpecularExpression = NewObject<UMaterialExpressionConstant>(Material);
		SpecularExpression->R = Specular;
		SpecularExpression->MaterialExpressionEditorX = -400;
		SpecularExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(SpecularExpression);
		Material->Specular.Expression = SpecularExpression;

		MaterialNodeY += MaterialNodeStepY;
	}
	
	if (InFlattenMaterial.RoughnessSamples.Num() == 1)
	{
		// Set Roughness to constant
		float Roughness = *(float*)(&InFlattenMaterial.RoughnessSamples[0].DWColor());
		auto RoughnessExpression = NewObject<UMaterialExpressionConstant>(Material);
		RoughnessExpression->R = Roughness;
		RoughnessExpression->MaterialExpressionEditorX = -400;
		RoughnessExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(RoughnessExpression);
		Material->Roughness.Expression = RoughnessExpression;

		MaterialNodeY += MaterialNodeStepY;
	}

	// Normal
	if (InFlattenMaterial.NormalSamples.Num() > 1)
	{
		const FString AssetName = TEXT("T_") + AssetBaseName + TEXT("_N");
		const bool bSRGB = false;
		UTexture2D* Texture = CreateTexture(InOuter, AssetBasePath + AssetName, InFlattenMaterial.NormalSize, InFlattenMaterial.NormalSamples, TC_Normalmap, TEXTUREGROUP_WorldNormalMap, Flags, bSRGB);
		OutGeneratedAssets.Add(Texture);
			
		auto NormalExpression = NewObject<UMaterialExpressionTextureSample>(Material);
		NormalExpression->Texture = Texture;
		NormalExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_Normal;
		NormalExpression->MaterialExpressionEditorX = -400;
		NormalExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(NormalExpression);
		Material->Normal.Expression = NormalExpression;

		MaterialNodeY+= MaterialNodeStepY;
	}

	if (InFlattenMaterial.EmissiveSamples.Num() == 1)
	{
		// Set Emissive to constant
		FColor EmissiveColor = InFlattenMaterial.EmissiveSamples[0];

		// Don't have to deal with black emissive color
		if (EmissiveColor != FColor(0, 0, 0, 255))
		{
			auto EmissiveColorExpression = NewObject<UMaterialExpressionConstant4Vector>(Material);
			EmissiveColorExpression->Constant = (EmissiveColor.ReinterpretAsLinear() * InFlattenMaterial.EmissiveScale);
			EmissiveColorExpression->MaterialExpressionEditorX = -400;
			EmissiveColorExpression->MaterialExpressionEditorY = MaterialNodeY;
			Material->Expressions.Add(EmissiveColorExpression);
			Material->EmissiveColor.Expression = EmissiveColorExpression;

			MaterialNodeY += MaterialNodeStepY;
		}
	}
	else if (InFlattenMaterial.EmissiveSamples.Num() > 1)
	{
		const FString AssetName = TEXT("T_") + AssetBaseName + TEXT("_E");
		const bool bSRGB = false;
		UTexture2D* Texture = CreateTexture(InOuter, AssetBasePath + AssetName, InFlattenMaterial.EmissiveSize, InFlattenMaterial.EmissiveSamples, TC_Default, TEXTUREGROUP_WorldNormalMap, Flags, bSRGB);
		OutGeneratedAssets.Add(Texture);

		//Assign emissive color to the material
		UMaterialExpressionTextureSample* EmissiveColorExpression = NewObject<UMaterialExpressionTextureSample>(Material);
		EmissiveColorExpression->Texture = Texture;
		EmissiveColorExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_LinearColor;
		EmissiveColorExpression->MaterialExpressionEditorX = -400;
		EmissiveColorExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(EmissiveColorExpression);

		UMaterialExpressionMultiply* EmissiveColorScale = NewObject<UMaterialExpressionMultiply>(Material);
		EmissiveColorScale->A.Expression = EmissiveColorExpression;
		EmissiveColorScale->ConstB = InFlattenMaterial.EmissiveScale;
		EmissiveColorScale->MaterialExpressionEditorX = -200;
		EmissiveColorScale->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(EmissiveColorScale);

		Material->EmissiveColor.Expression = EmissiveColorScale;
		MaterialNodeY += MaterialNodeStepY;
	}

	if (InFlattenMaterial.OpacitySamples.Num() == 1)
	{
		// Set Opacity to constant
		float Opacity = *(float*)(&InFlattenMaterial.OpacitySamples[0].DWColor());
		auto OpacityExpression = NewObject<UMaterialExpressionConstant>(Material);
		OpacityExpression->R = Opacity;
		OpacityExpression->MaterialExpressionEditorX = -400;
		OpacityExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(OpacityExpression);
		Material->Opacity.Expression = OpacityExpression;

		MaterialNodeY += MaterialNodeStepY;
	}
	else if (InFlattenMaterial.OpacitySamples.Num() > 1)
	{
		const FString AssetName = TEXT("T_") + AssetBaseName + TEXT("_O");
		const bool bSRGB = false;
		UTexture2D* Texture = CreateTexture(InOuter, AssetBasePath + AssetName, InFlattenMaterial.OpacitySize, InFlattenMaterial.OpacitySamples, TC_Grayscale, TEXTUREGROUP_WorldNormalMap, Flags, bSRGB);
		OutGeneratedAssets.Add(Texture);

		//Assign opacity to the material
		UMaterialExpressionTextureSample* OpacityExpression = NewObject<UMaterialExpressionTextureSample>(Material);
		OpacityExpression->Texture = Texture;
		OpacityExpression->SamplerType = EMaterialSamplerType::SAMPLERTYPE_LinearGrayscale;
		OpacityExpression->MaterialExpressionEditorX = -400;
		OpacityExpression->MaterialExpressionEditorY = MaterialNodeY;
		Material->Expressions.Add(OpacityExpression);
		Material->Opacity.Expression = OpacityExpression;
		MaterialNodeY += MaterialNodeStepY;
	}
							
	Material->PostEditChange();
	return Material;
}

UTexture2D* FMaterialUtilities::CreateTexture(UPackage* Outer, const FString& AssetLongName, FIntPoint Size, const TArray<FColor>& Samples, TextureCompressionSettings CompressionSettings, TextureGroup LODGroup, EObjectFlags Flags, bool bSRGB, const FGuid& SourceGuidHash)
{
	FCreateTexture2DParameters TexParams;
	TexParams.bUseAlpha = false;
	TexParams.CompressionSettings = CompressionSettings;
	TexParams.bDeferCompression = true;
	TexParams.bSRGB = bSRGB;
	TexParams.SourceGuidHash = SourceGuidHash;

	if (Outer == nullptr)
	{
		Outer = CreatePackage(NULL, *AssetLongName);
		Outer->FullyLoad();
		Outer->Modify();
	}

	UTexture2D* Texture = FImageUtils::CreateTexture2D(Size.X, Size.Y, Samples, Outer, FPackageName::GetShortName(AssetLongName), Flags, TexParams);
	Texture->LODGroup = LODGroup;
	Texture->PostEditChange();
		
	return Texture;
}

bool FMaterialUtilities::ExportBaseColor(ULandscapeComponent* LandscapeComponent, int32 TextureSize, TArray<FColor>& OutSamples)
{
	ALandscapeProxy* LandscapeProxy = LandscapeComponent->GetLandscapeProxy();

	FIntPoint ComponentOrigin = LandscapeComponent->GetSectionBase() - LandscapeProxy->LandscapeSectionOffset;
	FIntPoint ComponentSize(LandscapeComponent->ComponentSizeQuads, LandscapeComponent->ComponentSizeQuads);
	FVector MidPoint = FVector(ComponentOrigin, 0.f) + FVector(ComponentSize, 0.f)*0.5f;

	FVector LandscapeCenter = LandscapeProxy->GetTransform().TransformPosition(MidPoint);
	FVector LandscapeExtent = FVector(ComponentSize, 0.f)*LandscapeProxy->GetActorScale()*0.5f;

	FVector ViewOrigin = LandscapeCenter;
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(LandscapeProxy->GetActorRotation());
	ViewRotationMatrix *= FMatrix(FPlane(1, 0, 0, 0),
		FPlane(0, -1, 0, 0),
		FPlane(0, 0, -1, 0),
		FPlane(0, 0, 0, 1));

	const float ZOffset = WORLD_MAX;
	FMatrix ProjectionMatrix = FReversedZOrthoMatrix(
		LandscapeExtent.X,
		LandscapeExtent.Y,
		0.5f / ZOffset,
		ZOffset);

	FSceneInterface* Scene = LandscapeProxy->GetWorld()->Scene;

	// Hide all but the component
	TSet<FPrimitiveComponentId> HiddenPrimitives;
	for (auto PrimitiveComponentId : Scene->GetScenePrimitiveComponentIds())
	{
		HiddenPrimitives.Add(PrimitiveComponentId);
	}
	HiddenPrimitives.Remove(LandscapeComponent->SceneProxy->GetPrimitiveComponentId());
				
	FIntPoint TargetSize(TextureSize, TextureSize);

	// Render diffuse texture using BufferVisualizationMode=BaseColor
	static const FName BaseColorName("BaseColor");
	const float BaseColorGamma = 2.2f;
	RenderSceneToTexture(Scene, BaseColorName, ViewOrigin, ViewRotationMatrix, ProjectionMatrix, HiddenPrimitives, TargetSize, BaseColorGamma, OutSamples);
	return true;
}

FFlattenMaterial FMaterialUtilities::CreateFlattenMaterialWithSettings(const FMaterialProxySettings& InMaterialLODSettings)
{
	// Create new material.
	FFlattenMaterial Material;

	Material.DiffuseSize = InMaterialLODSettings.TextureSize;
	Material.SpecularSize = (InMaterialLODSettings.bSpecularMap) ? InMaterialLODSettings.TextureSize : FIntPoint::ZeroValue;
	Material.MetallicSize = (InMaterialLODSettings.bMetallicMap) ? InMaterialLODSettings.TextureSize : FIntPoint::ZeroValue;
	Material.RoughnessSize = (InMaterialLODSettings.bRoughnessMap) ? InMaterialLODSettings.TextureSize : FIntPoint::ZeroValue;
	Material.NormalSize = (InMaterialLODSettings.bNormalMap) ? InMaterialLODSettings.TextureSize : FIntPoint::ZeroValue;
	Material.EmissiveSize = (InMaterialLODSettings.bEmissiveMap) ? InMaterialLODSettings.TextureSize : FIntPoint::ZeroValue;
	Material.OpacitySize = (InMaterialLODSettings.bOpacityMap) ? InMaterialLODSettings.TextureSize : FIntPoint::ZeroValue;

	return Material;
}

void FMaterialUtilities::AnalyzeMaterial(UMaterialInterface* InMaterial, const struct FMaterialProxySettings& InMaterialSettings, int32& OutNumTexCoords, bool& OutUseVertexColor)
{
	OutUseVertexColor = false;
	OutNumTexCoords = 0;

	bool PropertyBeingBaked[MP_Normal + 1];	
	PropertyBeingBaked[MP_BaseColor] = true;
	PropertyBeingBaked[MP_Specular] = InMaterialSettings.bSpecularMap;
	PropertyBeingBaked[MP_Roughness] = InMaterialSettings.bRoughnessMap;
	PropertyBeingBaked[MP_Metallic] = InMaterialSettings.bMetallicMap;
	PropertyBeingBaked[MP_Normal] = InMaterialSettings.bNormalMap;
	PropertyBeingBaked[MP_Metallic] = InMaterialSettings.bOpacityMap;
	PropertyBeingBaked[MP_EmissiveColor] = InMaterialSettings.bEmissiveMap;

	for (int32 PropertyIndex = 0; PropertyIndex < ARRAY_COUNT(PropertyBeingBaked); ++PropertyIndex)
	{
		if (PropertyBeingBaked[PropertyIndex])
		{
			EMaterialProperty Property = (EMaterialProperty)PropertyIndex;

			if (PropertyIndex == MP_Opacity)
			{
				EBlendMode BlendMode = InMaterial->GetBlendMode();
				if (BlendMode == BLEND_Masked)
				{
					Property = MP_OpacityMask;
				}
				else if (IsTranslucentBlendMode(BlendMode))
				{
					Property = MP_Opacity;
				}
				else
				{
					continue;
				}
			}

			// Analyze this material channel.
			int32 NumTextureCoordinates = 0;
			bool bUseVertexColor = false;
			InMaterial->AnalyzeMaterialProperty(Property, NumTextureCoordinates, bUseVertexColor);
			// Accumulate data.
			OutNumTexCoords = FMath::Max(NumTextureCoordinates, OutNumTexCoords);
			OutUseVertexColor |= bUseVertexColor;
		}
	}
}

void FMaterialUtilities::RemapUniqueMaterialIndices(const TArray<UMaterialInterface*>& InMaterials, const TArray<struct FRawMeshExt>& InMeshData, const TMap<FMeshIdAndLOD, TArray<int32> >& InMaterialMap, const FMaterialProxySettings& InMaterialProxySettings, const bool bBakeVertexData, const bool bMergeMaterials, TArray<bool>& OutMeshShouldBakeVertexData, TMap<FMeshIdAndLOD, TArray<int32> >& OutMaterialMap, TArray<UMaterialInterface*>& OutMaterials)
{
	// Gather material properties
	TMap<UMaterialInterface*, int32> MaterialNumTexCoords;
	TMap<UMaterialInterface*, bool>  MaterialUseVertexColors;

	for (int32 MaterialIndex = 0; MaterialIndex < InMaterials.Num(); MaterialIndex++)
	{
		UMaterialInterface* Material = InMaterials[MaterialIndex];
		if (MaterialNumTexCoords.Find(Material) != nullptr)
		{
			// This material was already processed.
			continue;
		}

		if (!bBakeVertexData || !bMergeMaterials)
		{
			// We are not baking vertex data at all, don't analyze materials.
			MaterialNumTexCoords.Add(Material, 0);
			MaterialUseVertexColors.Add(Material, false);
			continue;
		}
		int32 NumTexCoords = 0;
		bool bUseVertexColors = false;
		FMaterialUtilities::AnalyzeMaterial(Material, InMaterialProxySettings, NumTexCoords, bUseVertexColors);
		MaterialNumTexCoords.Add(Material, NumTexCoords);
		MaterialUseVertexColors.Add(Material, bUseVertexColors);
	}

	// Check which meshes has vertex colors.
	TArray<bool> MeshHasVertexColors;
	MeshHasVertexColors.AddZeroed(InMeshData.Num());
	if (bBakeVertexData && bMergeMaterials)
	{
		for (int32 MeshIndex = 0; MeshIndex < InMeshData.Num(); MeshIndex++)
		{
			for (int32 LODIndex = 0; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
			{
				const FRawMesh Mesh = InMeshData[MeshIndex].MeshLODData[LODIndex].RawMesh;				
				bool bHasVertexColors = false;
				for (int32 WedgeIndex = 0; WedgeIndex < Mesh.WedgeColors.Num(); WedgeIndex++)
				{
					if (Mesh.WedgeColors[WedgeIndex] != FColor::White)
					{
						bHasVertexColors = true;
						break;
					}
				}
				MeshHasVertexColors[MeshIndex] |= bHasVertexColors;
				
			}
		}
	}

	// Build list of mesh's material properties.
	OutMeshShouldBakeVertexData.Empty();
	OutMeshShouldBakeVertexData.AddZeroed(InMeshData.Num());

	for (int32 MeshIndex = 0; MeshIndex < InMeshData.Num(); MeshIndex++)
	{
		for (int32 LODIndex = 0; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
		{
			if (InMeshData[MeshIndex].MeshLODData[LODIndex].RawMesh.VertexPositions.Num())
			{
				const TArray<int32>& MeshMaterialMap = InMaterialMap[FMeshIdAndLOD(MeshIndex, LODIndex)];
				int32 NumTexCoords = 0;
				bool bUseVertexColors = false;
				// Accumulate data of all materials.
				for (int32 LocalMaterialIndex = 0; LocalMaterialIndex < MeshMaterialMap.Num(); LocalMaterialIndex++)
				{
					UMaterialInterface* Material = InMaterials[MeshMaterialMap[LocalMaterialIndex]];
					NumTexCoords = FMath::Max(NumTexCoords, MaterialNumTexCoords[Material]);
					bUseVertexColors |= MaterialUseVertexColors[Material];
				}
				// Take into account presence of vertex colors in mesh.
				bUseVertexColors &= MeshHasVertexColors[MeshIndex];
				// Store data.
				MeshHasVertexColors[MeshIndex] = bUseVertexColors;
				OutMeshShouldBakeVertexData[MeshIndex] = bUseVertexColors || (NumTexCoords >= 2);
			}
			else
			{
				break;
			}
		}
	}

	// Build new material map.
	// Structure used to simplify material merging.
	struct FMeshMaterialData
	{
		UMaterialInterface* Material;
		UStaticMesh* Mesh;
		bool bHasVertexColors;

		FMeshMaterialData(UMaterialInterface* InMaterial, UStaticMesh* InMesh, bool bInHasVertexColors)
			: Material(InMaterial)
			, Mesh(InMesh)
			, bHasVertexColors(bInHasVertexColors)
		{
		}

		bool operator==(const FMeshMaterialData& Other) const
		{
			return Material == Other.Material && Mesh == Other.Mesh && bHasVertexColors == Other.bHasVertexColors;
		}
	};

	TArray<FMeshMaterialData> MeshMaterialData;
	OutMaterialMap.Empty();
	for (int32 MeshIndex = 0; MeshIndex < InMeshData.Num(); MeshIndex++)
	{
		for (int32 LODIndex = 0; LODIndex < MAX_STATIC_MESH_LODS; ++LODIndex)
		{
			if (InMeshData[MeshIndex].MeshLODData[LODIndex].RawMesh.VertexPositions.Num())
			{
				const TArray<int32>& MeshMaterialMap = InMaterialMap[FMeshIdAndLOD(MeshIndex, LODIndex)];
				TArray<int32>& NewMeshMaterialMap = OutMaterialMap.Add(FMeshIdAndLOD(MeshIndex, LODIndex));
				UStaticMesh* StaticMesh = InMeshData[MeshIndex].MeshLODData[LODIndex].SourceStaticMesh;

				if (!MeshHasVertexColors[MeshIndex])
				{
					// No vertex colors - could merge materials with other meshes.
					if (!OutMeshShouldBakeVertexData[MeshIndex])
					{
						// Set to 'nullptr' if don't need to bake vertex data to be able to merge materials with any meshes
						// which don't require vertex data baking too.
						StaticMesh = nullptr;
					}

					for (int32 LocalMaterialIndex = 0; LocalMaterialIndex < MeshMaterialMap.Num(); LocalMaterialIndex++)
					{
						FMeshMaterialData Data(InMaterials[MeshMaterialMap[LocalMaterialIndex]], StaticMesh, false);
						int32 Index = MeshMaterialData.Find(Data);
						if (Index == INDEX_NONE)
						{
							// Not found, add new entry.
							Index = MeshMaterialData.Add(Data);
						}
						NewMeshMaterialMap.Add(Index);
					}
				}
				else
				{
					// Mesh with vertex data baking, and with vertex colors - don't share materials at all.
					for (int32 LocalMaterialIndex = 0; LocalMaterialIndex < MeshMaterialMap.Num(); LocalMaterialIndex++)
					{
						FMeshMaterialData Data(InMaterials[MeshMaterialMap[LocalMaterialIndex]], StaticMesh, false);
						int32 Index = MeshMaterialData.Add(Data);
						NewMeshMaterialMap.Add(Index);
					}
				}
			}
			else
			{
				break;
			}			
		}
	}

	// Build new material list - simply extract MeshMaterialData[i].Material.
	OutMaterials.Empty();
	OutMaterials.AddUninitialized(MeshMaterialData.Num());
	for (int32 MaterialIndex = 0; MaterialIndex < MeshMaterialData.Num(); MaterialIndex++)
	{
		OutMaterials[MaterialIndex] = MeshMaterialData[MaterialIndex].Material;
	}
}

void FMaterialUtilities::OptimizeFlattenMaterial(FFlattenMaterial& InFlattenMaterial)
{
	// Try to optimize each individual property sample
	OptimizeSampleArray(InFlattenMaterial.DiffuseSamples, InFlattenMaterial.DiffuseSize);
	OptimizeSampleArray(InFlattenMaterial.NormalSamples, InFlattenMaterial.NormalSize);
	OptimizeSampleArray(InFlattenMaterial.MetallicSamples, InFlattenMaterial.MetallicSize);
	OptimizeSampleArray(InFlattenMaterial.RoughnessSamples, InFlattenMaterial.RoughnessSize);
	OptimizeSampleArray(InFlattenMaterial.SpecularSamples, InFlattenMaterial.SpecularSize);
	OptimizeSampleArray(InFlattenMaterial.OpacitySamples, InFlattenMaterial.OpacitySize);
	OptimizeSampleArray(InFlattenMaterial.EmissiveSamples, InFlattenMaterial.EmissiveSize);
}

bool FMaterialUtilities::ExportMaterial(struct FMaterialMergeData& InMaterialData, FFlattenMaterial& OutFlattenMaterial, struct FExportMaterialProxyCache* ProxyCache)
{
	UMaterialInterface* Material = InMaterialData.Material;
	UE_LOG(LogMaterialUtilities, Log, TEXT("Flattening material: %s"), *Material->GetName());

	if (ProxyCache)
	{
		// ExportMaterial was called with non-null CompiledMaterial. This means compiled shaders
		// should be stored outside, and could be re-used in next call to ExportMaterial.
		// FMaterialData already has "proxy cache" fiels, should swap it with CompiledMaterial,
		// and swap back before returning from this function.
		// Purpose of the following line: use compiled material cached from previous call.
		Exchange(ProxyCache, InMaterialData.ProxyCache);
	}

	// Make sure all the materials are loaded
	FullyLoadMaterialStatic(Material);

	// Precache all used textures, otherwise could get everything rendered with low-res textures.
	TArray<UTexture*> MaterialTextures;
	Material->GetUsedTextures(MaterialTextures, EMaterialQualityLevel::Num, true, GMaxRHIFeatureLevel, true);

	for (UTexture* Texture : MaterialTextures)
	{
		if (Texture != NULL)
		{
			UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
			if (Texture2D)
			{
				Texture2D->SetForceMipLevelsToBeResident(30.0f, true);
				Texture2D->WaitForStreaming();
			}
		}		
	}

	// Determine whether or not certain properties can be rendered
	bool bRenderNormal = Material->GetMaterial()->HasNormalConnected() || (Material->GetMaterial()->bUseMaterialAttributes) || Material->IsPropertyActive(MP_Normal);
	bool bRenderEmissive = Material->GetMaterial()->EmissiveColor.IsConnected() && Material->IsPropertyActive(MP_EmissiveColor);
	bool bRenderOpacityMask = Material->IsPropertyActive(MP_OpacityMask) && Material->GetBlendMode() == BLEND_Masked;
	bool bRenderOpacity = Material->IsPropertyActive(MP_Opacity) && IsTranslucentBlendMode(Material->GetBlendMode());
	check(!bRenderOpacity || !bRenderOpacityMask);

	// Compile shaders and render flatten material.	
	RenderMaterialPropertyToTexture(InMaterialData, MP_BaseColor, false, PF_B8G8R8A8, OutFlattenMaterial.DiffuseSize, OutFlattenMaterial.DiffuseSamples);
	RenderMaterialPropertyToTexture(InMaterialData, MP_Metallic, false, PF_B8G8R8A8, OutFlattenMaterial.MetallicSize, OutFlattenMaterial.MetallicSamples);
	RenderMaterialPropertyToTexture(InMaterialData, MP_Specular, false, PF_B8G8R8A8, OutFlattenMaterial.SpecularSize, OutFlattenMaterial.SpecularSamples);
	RenderMaterialPropertyToTexture(InMaterialData, MP_Roughness, false, PF_B8G8R8A8, OutFlattenMaterial.RoughnessSize, OutFlattenMaterial.RoughnessSamples);
	if (bRenderNormal)
	{
		RenderMaterialPropertyToTexture(InMaterialData, MP_Normal, true, PF_B8G8R8A8, OutFlattenMaterial.NormalSize, OutFlattenMaterial.NormalSamples);
	}
	if (bRenderOpacityMask)
	{
		RenderMaterialPropertyToTexture(InMaterialData, MP_OpacityMask, true, PF_B8G8R8A8, OutFlattenMaterial.OpacitySize, OutFlattenMaterial.OpacitySamples);
	}
	if (bRenderOpacity)
	{
		// Number of blend modes, let's UMaterial decide whether it wants this property
		RenderMaterialPropertyToTexture(InMaterialData, MP_Opacity, true, PF_B8G8R8A8, OutFlattenMaterial.OpacitySize, OutFlattenMaterial.OpacitySamples);
	}
	if (bRenderEmissive)
	{
		// PF_FloatRGBA is here to be able to render and read HDR image using ReadFloat16Pixels()
		RenderMaterialPropertyToTexture(InMaterialData, MP_EmissiveColor, false, PF_FloatRGBA, OutFlattenMaterial.EmissiveSize, OutFlattenMaterial.EmissiveSamples);
		OutFlattenMaterial.EmissiveScale = InMaterialData.EmissiveScale;
	}	

	OutFlattenMaterial.MaterialId = Material->GetLightingGuid();

	// Swap back the proxy cache
	if (ProxyCache)
	{
		// Store compiled material to external cache.
		Exchange(ProxyCache, InMaterialData.ProxyCache);
	}

	UE_LOG(LogMaterialUtilities, Log, TEXT("Material flattening done. (%s)"), *Material->GetName());

	return true;
}

bool FMaterialUtilities::RenderMaterialPropertyToTexture(struct FMaterialMergeData& InMaterialData, EMaterialProperty InMaterialProperty, bool bInForceLinearGamma, EPixelFormat InPixelFormat, FIntPoint& InTargetSize, TArray<FColor>& OutSamples)
{
	if (InTargetSize.X == 0 || InTargetSize.Y == 0)
	{
		return false;
	}

	FMaterialRenderProxy* MaterialProxy = nullptr;

	check(InMaterialProperty >= 0 && InMaterialProperty < ARRAY_COUNT(InMaterialData.ProxyCache->Proxies));
	if (InMaterialData.ProxyCache->Proxies[InMaterialProperty])
	{
		MaterialProxy = InMaterialData.ProxyCache->Proxies[InMaterialProperty];
	}
	else
	{
		MaterialProxy = InMaterialData.ProxyCache->Proxies[InMaterialProperty] = new FExportMaterialProxy(InMaterialData.Material, InMaterialProperty);
	}
	
	if (MaterialProxy == nullptr)
	{
		return false;
	}
	
	// Disallow garbage collection of RenderTarget.
	check(CurrentlyRendering == false);
	CurrentlyRendering = true;

	UTextureRenderTarget2D* RenderTarget = CreateRenderTarget(bInForceLinearGamma, InPixelFormat, InTargetSize);

	OutSamples.Empty(InTargetSize.X * InTargetSize.Y);
	bool bResult = FMeshRenderer::RenderMaterial(
		InMaterialData,
		MaterialProxy,
		InMaterialProperty,
		RenderTarget,
		OutSamples);

	// Check for uniform value, perhaps this can be determined before rendering the material, see WillGenerateUniformData (LightmassRender)
	bool bIsUniform = true;
	FColor MaxColor(0, 0, 0, 0);
	if (bResult)
	{
		// Find maximal color value
		int32 MaxColorValue = 0;
		for (int32 Index = 0; Index < OutSamples.Num(); Index++)
		{
			FColor Color = OutSamples[Index];
			int32 ColorValue = Color.R + Color.G + Color.B + Color.A;
			if (ColorValue > MaxColorValue)
			{
				MaxColorValue = ColorValue;
				MaxColor = Color;
			}
		}

		// Fill background with maximal color value and render again		
		RenderTarget->ClearColor = FLinearColor(MaxColor);
		TArray<FColor> OutSamples2;
		FMeshRenderer::RenderMaterial(
			InMaterialData,
			MaterialProxy,
			InMaterialProperty,
			RenderTarget,
			OutSamples2);
		for (int32 Index = 0; Index < OutSamples2.Num(); Index++)
		{
			FColor Color = OutSamples2[Index];
			if (Color != MaxColor)
			{
				bIsUniform = false;
				break;
			}
		}
	}

	// Uniform value
	if (bIsUniform)
	{
		InTargetSize = FIntPoint(1, 1);
		OutSamples.Empty();
		OutSamples.Add(MaxColor);
	}

	CurrentlyRendering = false;

	return bResult;
}

UTextureRenderTarget2D* FMaterialUtilities::CreateRenderTarget(bool bInForceLinearGamma, EPixelFormat InPixelFormat, FIntPoint& InTargetSize)
{
	// Find any pooled render target with suitable properties.
	for (int32 RTIndex = 0; RTIndex < RenderTargetPool.Num(); RTIndex++)
	{
		UTextureRenderTarget2D* RenderTarget = RenderTargetPool[RTIndex];
		if (RenderTarget->SizeX == InTargetSize.X &&
			RenderTarget->SizeY == InTargetSize.Y &&
			RenderTarget->OverrideFormat == InPixelFormat &&
			RenderTarget->bForceLinearGamma == bInForceLinearGamma)
		{
			return RenderTarget;
		}
	}

	// Not found - create a new one.
	UTextureRenderTarget2D* NewRenderTarget = NewObject<UTextureRenderTarget2D>();
	check(NewRenderTarget);
	NewRenderTarget->AddToRoot();
	NewRenderTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	NewRenderTarget->TargetGamma = 0.0f;
	NewRenderTarget->InitCustomFormat(InTargetSize.X, InTargetSize.Y, InPixelFormat, bInForceLinearGamma);

	RenderTargetPool.Add(NewRenderTarget);
	return NewRenderTarget;
}

void FMaterialUtilities::ClearRenderTargetPool()
{
	if (CurrentlyRendering)
	{
		// Just in case - if garbage collection will happen during rendering, don't allow to GC used render target.
		return;
	}

	// Allow garbage collecting of all render targets.
	for (int32 RTIndex = 0; RTIndex < RenderTargetPool.Num(); RTIndex++)
	{
		RenderTargetPool[RTIndex]->RemoveFromRoot();
	}
	RenderTargetPool.Empty();
}

void FMaterialUtilities::FullyLoadMaterialStatic(UMaterialInterface* Material)
{
	FLinkerLoad* Linker = Material->GetLinker();
	if (Linker == nullptr)
	{
		return;
	}
	// Load material and all expressions.
	Linker->LoadAllObjects(true);
	Material->ConditionalPostLoad();
	// Now load all used textures.
	TArray<UTexture*> Textures;
	Material->GetUsedTextures(Textures, EMaterialQualityLevel::Num, true, ERHIFeatureLevel::SM5, true);
	for (int32 i = 0; i < Textures.Num(); ++i)
	{
		UTexture* Texture = Textures[i];
		FLinkerLoad* Linker = Material->GetLinker();
		if (Linker)
		{
			Linker->Preload(Texture);
		}
	}
}

void FMaterialUtilities::OptimizeSampleArray(TArray<FColor>& InSamples, FIntPoint& InSampleSize)
{
	if (InSamples.Num() > 1)
	{
		const FColor ColourValue = InSamples[0];
		bool bConstantValue = true;

		for (FColor& Sample : InSamples)
		{
			if (ColourValue != Sample)
			{
				bConstantValue = false;
				break;
			}
		}

		if (bConstantValue)
		{
			InSamples.Empty();
			InSamples.Add(ColourValue);
			InSampleSize = FIntPoint(1, 1);
		}
	}	
}

FExportMaterialProxyCache::FExportMaterialProxyCache()
{
	FMemory::Memzero(Proxies);
}

FExportMaterialProxyCache::~FExportMaterialProxyCache()
{
	Release();
}

void FExportMaterialProxyCache::Release()
{
	for (int32 PropertyIndex = 0; PropertyIndex < ARRAY_COUNT(Proxies); PropertyIndex++)
	{
		FMaterialRenderProxy* Proxy = Proxies[PropertyIndex];
		if (Proxy)
		{
			delete Proxy;
			Proxies[PropertyIndex] = nullptr;
		}
	}
}

