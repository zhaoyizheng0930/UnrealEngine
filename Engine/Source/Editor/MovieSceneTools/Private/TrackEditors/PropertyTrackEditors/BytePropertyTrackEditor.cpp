// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneToolsPrivatePCH.h"
#include "BytePropertyTrackEditor.h"
#include "BytePropertySection.h"
#include "MovieSceneByteTrack.h"
#include "MovieSceneSequence.h"


TSharedRef<ISequencerTrackEditor> FBytePropertyTrackEditor::CreateTrackEditor( TSharedRef<ISequencer> OwningSequencer )
{
	return MakeShareable(new FBytePropertyTrackEditor(OwningSequencer));
}


TSharedRef<FPropertySection> FBytePropertyTrackEditor::MakePropertySectionInterface( UMovieSceneSection& SectionObject, UMovieSceneTrack& Track )
{
	return MakeShareable( new FBytePropertySection( SectionObject, Track.GetTrackName(), Cast<UMovieSceneByteTrack>( SectionObject.GetOuter() )->GetEnum() ) );
}


UEnum* GetEnumForByteTrack(TSharedPtr<ISequencer> Sequencer, const FGuid& OwnerObjectHandle, FName PropertyName, UMovieSceneByteTrack* ByteTrack)
{
	
	UObject* RuntimeObject = Sequencer->GetFocusedMovieSceneSequence()->FindObject(OwnerObjectHandle);
	TSet<UEnum*> PropertyEnums;

	if (RuntimeObject != nullptr)
	{
		UProperty* Property = RuntimeObject->GetClass()->FindPropertyByName(PropertyName);
		if (Property != nullptr)
		{
			UByteProperty* ByteProperty = Cast<UByteProperty>(Property);
			if (ByteProperty != nullptr && ByteProperty->Enum != nullptr)
			{
				PropertyEnums.Add(ByteProperty->Enum);
			}
		}
	}

	UEnum* TrackEnum;

	if (PropertyEnums.Num() == 1)
	{
		TrackEnum = PropertyEnums.Array()[0];
	}
	else
	{
		TrackEnum = nullptr;
	}

	return TrackEnum;
}


UMovieSceneTrack* FBytePropertyTrackEditor::AddTrack(UMovieScene* FocusedMovieScene, const FGuid& ObjectHandle, TSubclassOf<class UMovieSceneTrack> TrackClass, FName UniqueTypeName)
{
	UMovieSceneTrack* NewTrack = FPropertyTrackEditor::AddTrack(FocusedMovieScene, ObjectHandle, TrackClass, UniqueTypeName);
	UMovieSceneByteTrack* ByteTrack = Cast<UMovieSceneByteTrack>(NewTrack);
	UEnum* TrackEnum = GetEnumForByteTrack(GetSequencer(), ObjectHandle, UniqueTypeName, ByteTrack);

	if (TrackEnum != nullptr)
	{
		ByteTrack->SetEnum(TrackEnum);
	}

	return NewTrack;
}


void FBytePropertyTrackEditor::GenerateKeysFromPropertyChanged( const FPropertyChangedParams& PropertyChangedParams, TArray<uint8>& GeneratedKeys )
{
	GeneratedKeys.Add( PropertyChangedParams.GetPropertyValue<uint8>() );
}
