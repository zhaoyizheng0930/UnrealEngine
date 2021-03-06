// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"

UDefaultSizedThumbnailRenderer::UDefaultSizedThumbnailRenderer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UDefaultSizedThumbnailRenderer::GetThumbnailSize(UObject*, float Zoom, uint32& OutWidth, uint32& OutHeight) const
{
	OutWidth = FMath::TruncToInt(DefaultSizeX * Zoom);
	OutHeight = FMath::TruncToInt(DefaultSizeY * Zoom);
}
