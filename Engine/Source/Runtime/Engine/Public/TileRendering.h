// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TileRendering.h: Simple tile rendering implementation.
=============================================================================*/

#ifndef _INC_TILERENDERING
#define _INC_TILERENDERING

class FTileRenderer
{
public:

	/**
	 * Draw a tile at the given location and size, using the given UVs
	 * (UV = [0..1]
	 */
	ENGINE_API static void DrawTile(FRHICommandListImmediate& RHICmdList, const class FSceneView& View, const FMaterialRenderProxy* MaterialRenderProxy, bool bNeedsToSwitchVerticalAxis, float X, float Y, float SizeX, float SizeY, float U, float V, float SizeU, float SizeV, bool bIsHitTesting = false, const FHitProxyId HitProxyId = FHitProxyId(), const FColor InVertexColor = FColor(255, 255, 255, 255));
	
private:

	/** This class never needs to be instantiated. */
	FTileRenderer() {}
};

#endif
