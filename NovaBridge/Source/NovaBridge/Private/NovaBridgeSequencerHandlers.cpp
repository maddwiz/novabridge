#include "NovaBridgeModule.h"
#include "NovaBridgeEditorInternals.h"

#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Channels/MovieSceneDoubleChannel.h"

void NovaBridgeSetPlaybackTime(ULevelSequencePlayer* Player, const float TimeSeconds, const bool bScrub)
{
	if (!Player)
	{
		return;
	}

	FMovieSceneSequencePlaybackParams Params;
	Params.PositionType = EMovieScenePositionType::Time;
	Params.Time = TimeSeconds;
	Params.UpdateMethod = bScrub ? EUpdatePositionMethod::Scrub : EUpdatePositionMethod::Jump;
	Player->SetPlaybackPosition(Params);
}

namespace
{
FGuid NovaBridgeFindBinding(ULevelSequence* Sequence, AActor* Actor, UWorld* World)
{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	FMovieSceneSequencePlaybackSettings Settings;
	ALevelSequenceActor* SequenceActor = nullptr;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
	if (!Player)
	{
		return FGuid();
	}

	FGuid Binding = Sequence->FindBindingFromObject(Actor, Player->GetSharedPlaybackState());
	Player->Stop();
	if (SequenceActor)
	{
		SequenceActor->Destroy();
	}
	return Binding;
#else
	return Sequence->FindBindingFromObject(Actor, World);
#endif
}
} // namespace

bool FNovaBridgeModule::HandleSequencerCreate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString Name = Body->GetStringField(TEXT("name"));
	const FString Path = Body->HasField(TEXT("path")) ? Body->GetStringField(TEXT("path")) : TEXT("/Game");
	const float DurationSeconds = Body->HasField(TEXT("duration_seconds")) ? static_cast<float>(Body->GetNumberField(TEXT("duration_seconds"))) : 10.0f;
	const int32 Fps = Body->HasField(TEXT("fps")) ? FMath::Clamp(static_cast<int32>(Body->GetNumberField(TEXT("fps"))), 1, 120) : 30;

	if (Name.IsEmpty())
	{
		SendErrorResponse(OnComplete, TEXT("Missing 'name'"));
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Name, Path, DurationSeconds, Fps]()
	{
		const FString PackagePath = Path / Name;
		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create sequence package"), 500);
			return;
		}

		ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, FName(*Name), RF_Public | RF_Standalone);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create sequence asset"), 500);
			return;
		}

		Sequence->Initialize();
		UMovieScene* MovieScene = Sequence->GetMovieScene();
		if (!MovieScene)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to initialize movie scene"), 500);
			return;
		}

		const FFrameRate DisplayRate(Fps, 1);
		MovieScene->SetDisplayRate(DisplayRate);
		MovieScene->SetTickResolutionDirectly(DisplayRate);
		const FFrameNumber DurationFrames = DisplayRate.AsFrameNumber(FMath::Max(0.1f, DurationSeconds));
		MovieScene->SetPlaybackRange(0, DurationFrames.Value);

		FAssetRegistryModule::AssetCreated(Sequence);
		Sequence->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), Sequence->GetPathName());
		Result->SetNumberField(TEXT("duration_seconds"), DurationSeconds);
		Result->SetNumberField(TEXT("fps"), Fps);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerAddTrack(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const FString ActorName = Body->GetStringField(TEXT("actor_name"));
	const FString TrackType = Body->HasField(TEXT("track_type")) ? Body->GetStringField(TEXT("track_type")).ToLower() : TEXT("transform");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, ActorName, TrackType]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
			return;
		}

		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		FGuid Binding = NovaBridgeFindBinding(Sequence, Actor, World);
		if (!Binding.IsValid())
		{
			Binding = MovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
			Sequence->BindPossessableObject(Binding, *Actor, World);
		}

		if (TrackType != TEXT("transform"))
		{
			SendErrorResponse(OnComplete, TEXT("Only track_type='transform' is supported in v1"), 400);
			return;
		}

		UMovieScene3DTransformTrack* Track = MovieScene->FindTrack<UMovieScene3DTransformTrack>(Binding);
		if (!Track)
		{
			Track = MovieScene->AddTrack<UMovieScene3DTransformTrack>(Binding);
		}
		if (!Track)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create transform track"), 500);
			return;
		}

		if (Track->GetAllSections().Num() == 0)
		{
			UMovieSceneSection* NewSection = Track->CreateNewSection();
			Track->AddSection(*NewSection);
		}

		Sequence->MarkPackageDirty();
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetStringField(TEXT("actor_name"), ActorName);
		Result->SetStringField(TEXT("track_type"), TrackType);
		Result->SetStringField(TEXT("binding"), Binding.ToString());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerSetKeyframe(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const FString ActorName = Body->GetStringField(TEXT("actor_name"));
	const float TimeSeconds = Body->HasField(TEXT("time")) ? static_cast<float>(Body->GetNumberField(TEXT("time"))) : 0.0f;
	const FString TrackType = Body->HasField(TEXT("track_type")) ? Body->GetStringField(TEXT("track_type")).ToLower() : TEXT("transform");

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, ActorName, TimeSeconds, TrackType, Body]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		if (TrackType != TEXT("transform"))
		{
			SendErrorResponse(OnComplete, TEXT("Only track_type='transform' is supported in v1"), 400);
			return;
		}

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
			return;
		}

		AActor* Actor = FindActorByName(ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		FGuid Binding = NovaBridgeFindBinding(Sequence, Actor, World);
		if (!Binding.IsValid())
		{
			Binding = MovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
			Sequence->BindPossessableObject(Binding, *Actor, World);
		}

		UMovieScene3DTransformTrack* Track = MovieScene->FindTrack<UMovieScene3DTransformTrack>(Binding);
		if (!Track)
		{
			Track = MovieScene->AddTrack<UMovieScene3DTransformTrack>(Binding);
		}
		if (!Track)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create transform track"), 500);
			return;
		}

		UMovieScene3DTransformSection* Section = nullptr;
		if (Track->GetAllSections().Num() == 0)
		{
			UMovieSceneSection* NewSection = Track->CreateNewSection();
			if (!NewSection)
			{
				SendErrorResponse(OnComplete, TEXT("Failed to create transform section"), 500);
				return;
			}
			Track->AddSection(*NewSection);
			Section = Cast<UMovieScene3DTransformSection>(NewSection);
		}
		else
		{
			Section = Cast<UMovieScene3DTransformSection>(Track->GetAllSections()[0]);
		}
		if (!Section)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create transform section"), 500);
			return;
		}

		FVector Location = Actor->GetActorLocation();
		FRotator Rotation = Actor->GetActorRotation();
		FVector Scale = Actor->GetActorScale3D();

		if (Body->HasTypedField<EJson::Object>(TEXT("location")))
		{
			const TSharedPtr<FJsonObject> Loc = Body->GetObjectField(TEXT("location"));
			Location.X = Loc->HasField(TEXT("x")) ? Loc->GetNumberField(TEXT("x")) : Location.X;
			Location.Y = Loc->HasField(TEXT("y")) ? Loc->GetNumberField(TEXT("y")) : Location.Y;
			Location.Z = Loc->HasField(TEXT("z")) ? Loc->GetNumberField(TEXT("z")) : Location.Z;
		}
		if (Body->HasTypedField<EJson::Object>(TEXT("rotation")))
		{
			const TSharedPtr<FJsonObject> Rot = Body->GetObjectField(TEXT("rotation"));
			Rotation.Pitch = Rot->HasField(TEXT("pitch")) ? Rot->GetNumberField(TEXT("pitch")) : Rotation.Pitch;
			Rotation.Yaw = Rot->HasField(TEXT("yaw")) ? Rot->GetNumberField(TEXT("yaw")) : Rotation.Yaw;
			Rotation.Roll = Rot->HasField(TEXT("roll")) ? Rot->GetNumberField(TEXT("roll")) : Rotation.Roll;
		}
		if (Body->HasTypedField<EJson::Object>(TEXT("scale")))
		{
			const TSharedPtr<FJsonObject> ScaleObj = Body->GetObjectField(TEXT("scale"));
			Scale.X = ScaleObj->HasField(TEXT("x")) ? ScaleObj->GetNumberField(TEXT("x")) : Scale.X;
			Scale.Y = ScaleObj->HasField(TEXT("y")) ? ScaleObj->GetNumberField(TEXT("y")) : Scale.Y;
			Scale.Z = ScaleObj->HasField(TEXT("z")) ? ScaleObj->GetNumberField(TEXT("z")) : Scale.Z;
		}

		const FFrameNumber KeyFrame = MovieScene->GetTickResolution().AsFrameNumber(TimeSeconds);
		TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		if (Channels.Num() >= 9)
		{
			Channels[0]->AddCubicKey(KeyFrame, Location.X);
			Channels[1]->AddCubicKey(KeyFrame, Location.Y);
			Channels[2]->AddCubicKey(KeyFrame, Location.Z);
			Channels[3]->AddCubicKey(KeyFrame, Rotation.Roll);
			Channels[4]->AddCubicKey(KeyFrame, Rotation.Pitch);
			Channels[5]->AddCubicKey(KeyFrame, Rotation.Yaw);
			Channels[6]->AddCubicKey(KeyFrame, Scale.X);
			Channels[7]->AddCubicKey(KeyFrame, Scale.Y);
			Channels[8]->AddCubicKey(KeyFrame, Scale.Z);
		}

		Section->SetRange(TRange<FFrameNumber>::All());
		Sequence->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetStringField(TEXT("actor_name"), ActorName);
		Result->SetNumberField(TEXT("time"), TimeSeconds);
		Result->SetNumberField(TEXT("frame"), KeyFrame.Value);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerPlay(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const bool bLoop = Body->HasField(TEXT("loop")) && Body->GetBoolField(TEXT("loop"));
	const float StartTime = Body->HasField(TEXT("start_time")) ? static_cast<float>(Body->GetNumberField(TEXT("start_time"))) : 0.0f;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, bLoop, StartTime]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
			return;
		}

		FMovieSceneSequencePlaybackSettings Settings;
		Settings.bAutoPlay = false;
		Settings.LoopCount.Value = bLoop ? -1 : 0;

		ALevelSequenceActor* SequenceActor = nullptr;
		ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
		if (!Player)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create sequence player"), 500);
			return;
		}

		if (StartTime > 0.f)
		{
			NovaBridgeSetPlaybackTime(Player, StartTime, false);
		}
		Player->Play();

		SequencePlayers.Add(SequencePath, Player);
		if (SequenceActor)
		{
			SequenceActors.Add(SequencePath, SequenceActor);
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetBoolField(TEXT("playing"), true);
		Result->SetBoolField(TEXT("loop"), bLoop);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerStop(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	const FString SequencePath = (Body && Body->HasField(TEXT("sequence"))) ? Body->GetStringField(TEXT("sequence")) : FString();

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath]()
	{
		int32 Stopped = 0;
		if (!SequencePath.IsEmpty())
		{
			if (TWeakObjectPtr<ULevelSequencePlayer>* PlayerPtr = SequencePlayers.Find(SequencePath))
			{
				if (PlayerPtr->IsValid())
				{
					(*PlayerPtr)->Stop();
					Stopped++;
				}
			}
		}
		else
		{
			for (TPair<FString, TWeakObjectPtr<ULevelSequencePlayer>>& Pair : SequencePlayers)
			{
				if (Pair.Value.IsValid())
				{
					Pair.Value->Stop();
					Stopped++;
				}
			}
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetNumberField(TEXT("stopped_players"), Stopped);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerScrub(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const float TimeSeconds = Body->HasField(TEXT("time")) ? static_cast<float>(Body->GetNumberField(TEXT("time"))) : 0.0f;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, TimeSeconds]()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No world"), 500);
			return;
		}

		ULevelSequencePlayer* Player = nullptr;
		if (TWeakObjectPtr<ULevelSequencePlayer>* Existing = SequencePlayers.Find(SequencePath))
		{
			Player = Existing->Get();
		}
		if (!Player)
		{
			ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
			if (!Sequence)
			{
				SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
				return;
			}
			FMovieSceneSequencePlaybackSettings Settings;
			ALevelSequenceActor* SequenceActor = nullptr;
			Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
			if (!Player)
			{
				SendErrorResponse(OnComplete, TEXT("Failed to create sequence player"), 500);
				return;
			}
			SequencePlayers.Add(SequencePath, Player);
			if (SequenceActor)
			{
				SequenceActors.Add(SequencePath, SequenceActor);
			}
		}

		NovaBridgeSetPlaybackTime(Player, TimeSeconds, true);
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetNumberField(TEXT("time"), TimeSeconds);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleSequencerInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		TArray<TSharedPtr<FJsonValue>> Active;
		for (const TPair<FString, TWeakObjectPtr<ULevelSequencePlayer>>& Pair : SequencePlayers)
		{
			if (!Pair.Value.IsValid())
			{
				continue;
			}
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetStringField(TEXT("sequence"), Pair.Key);
			Obj->SetBoolField(TEXT("playing"), Pair.Value->IsPlaying());
			Obj->SetNumberField(TEXT("time_seconds"), Pair.Value->GetCurrentTime().AsSeconds());
			Active.Add(MakeShareable(new FJsonValueObject(Obj)));
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetArrayField(TEXT("players"), Active);
		Result->SetNumberField(TEXT("count"), Active.Num());
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}
