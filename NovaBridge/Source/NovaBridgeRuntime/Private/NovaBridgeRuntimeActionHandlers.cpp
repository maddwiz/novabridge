#include "NovaBridgeRuntimeModule.h"

#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Misc/Base64.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneSequencePlayer.h"
#include "UnrealClient.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#if NOVABRIDGE_WITH_PCG
#include "PCGComponent.h"
#include "PCGVolume.h"
#endif

namespace
{
static const TCHAR* RuntimeModeName = TEXT("runtime");

struct FRuntimeSequenceState
{
	FCriticalSection Mutex;
	TMap<FString, TWeakObjectPtr<ULevelSequencePlayer>> Players;
	TMap<FString, TWeakObjectPtr<ALevelSequenceActor>> Actors;
};

FRuntimeSequenceState& RuntimeSequenceState()
{
	static FRuntimeSequenceState State;
	return State;
}

UWorld* ResolveRuntimeWorld()
{
	if (!GEngine)
	{
		return nullptr;
	}

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (!World)
		{
			continue;
		}

		if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::GameRPC)
		{
			return World;
		}
	}

	return nullptr;
}

AActor* FindActorByNameRuntime(UWorld* World, const FString& Name)
{
	if (!World || Name.IsEmpty())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (Actor->GetName() == Name || Actor->GetActorNameOrLabel() == Name)
		{
			return Actor;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> ActorToJsonRuntime(AActor* Actor)
{
	const TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Actor)
	{
		Obj->SetStringField(TEXT("status"), TEXT("error"));
		Obj->SetStringField(TEXT("error"), TEXT("Actor is null"));
		return Obj;
	}

	Obj->SetStringField(TEXT("name"), Actor->GetName());
	Obj->SetStringField(TEXT("label"), Actor->GetActorNameOrLabel());
	Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	Obj->SetBoolField(TEXT("pending_kill"), Actor->IsPendingKillPending());

	const FVector Location = Actor->GetActorLocation();
	const FRotator Rotation = Actor->GetActorRotation();
	const FVector Scale = Actor->GetActorScale3D();

	const TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), Location.X);
	LocationObj->SetNumberField(TEXT("y"), Location.Y);
	LocationObj->SetNumberField(TEXT("z"), Location.Z);
	Obj->SetObjectField(TEXT("location"), LocationObj);

	const TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
	RotationObj->SetNumberField(TEXT("pitch"), Rotation.Pitch);
	RotationObj->SetNumberField(TEXT("yaw"), Rotation.Yaw);
	RotationObj->SetNumberField(TEXT("roll"), Rotation.Roll);
	Obj->SetObjectField(TEXT("rotation"), RotationObj);

	const TSharedPtr<FJsonObject> ScaleObj = MakeShared<FJsonObject>();
	ScaleObj->SetNumberField(TEXT("x"), Scale.X);
	ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
	ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
	Obj->SetObjectField(TEXT("scale"), ScaleObj);

	return Obj;
}

bool JsonValueToVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() != 3)
		{
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (!Arr[0].IsValid() || !Arr[1].IsValid() || !Arr[2].IsValid()
			|| !Arr[0]->TryGetNumber(X) || !Arr[1]->TryGetNumber(Y) || !Arr[2]->TryGetNumber(Z))
		{
			return false;
		}

		OutVector = FVector(X, Y, Z);
		return true;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid() || !Obj->HasField(TEXT("x")) || !Obj->HasField(TEXT("y")) || !Obj->HasField(TEXT("z")))
		{
			return false;
		}

		OutVector = FVector(
			Obj->GetNumberField(TEXT("x")),
			Obj->GetNumberField(TEXT("y")),
			Obj->GetNumberField(TEXT("z"))
		);
		return true;
	}

	return false;
}

bool JsonValueToRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		if (Arr.Num() != 3)
		{
			return false;
		}

		double Pitch = 0.0;
		double Yaw = 0.0;
		double Roll = 0.0;
		if (!Arr[0].IsValid() || !Arr[1].IsValid() || !Arr[2].IsValid()
			|| !Arr[0]->TryGetNumber(Pitch) || !Arr[1]->TryGetNumber(Yaw) || !Arr[2]->TryGetNumber(Roll))
		{
			return false;
		}

		OutRotator = FRotator(Pitch, Yaw, Roll);
		return true;
	}

	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			return false;
		}

		if (Obj->HasField(TEXT("pitch")) && Obj->HasField(TEXT("yaw")) && Obj->HasField(TEXT("roll")))
		{
			OutRotator = FRotator(
				Obj->GetNumberField(TEXT("pitch")),
				Obj->GetNumberField(TEXT("yaw")),
				Obj->GetNumberField(TEXT("roll"))
			);
			return true;
		}

		if (Obj->HasField(TEXT("x")) && Obj->HasField(TEXT("y")) && Obj->HasField(TEXT("z")))
		{
			OutRotator = FRotator(
				Obj->GetNumberField(TEXT("x")),
				Obj->GetNumberField(TEXT("y")),
				Obj->GetNumberField(TEXT("z"))
			);
			return true;
		}
	}

	return false;
}

bool JsonValueToImportText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
{
	if (!Value.IsValid())
	{
		return false;
	}

	switch (Value->Type)
	{
	case EJson::String:
		OutText = Value->AsString();
		return true;
	case EJson::Number:
		OutText = FString::SanitizeFloat(Value->AsNumber());
		return true;
	case EJson::Boolean:
		OutText = Value->AsBool() ? TEXT("True") : TEXT("False");
		return true;
	case EJson::Null:
		OutText.Empty();
		return true;
	default:
		return false;
	}
}

bool SetRuntimeActorPropertyValue(AActor* Actor, const FString& PropertyName, const FString& Value, FString& OutError)
{
	if (!Actor)
	{
		OutError = TEXT("Actor is null");
		return false;
	}

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop)
	{
		for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
		{
			if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				Prop = *It;
				break;
			}
		}
	}
	if (!Prop)
	{
		OutError = FString::Printf(TEXT("Property not found: %s"), *PropertyName);
		return false;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
	if (!Prop->ImportText_Direct(*Value, ValuePtr, Actor, PPF_None))
	{
		OutError = FString::Printf(TEXT("Failed to set property: %s"), *PropertyName);
		return false;
	}

	return true;
}

bool CaptureRuntimeViewportPng(TArray64<uint8>& OutPngData, int32& OutWidth, int32& OutHeight, FString& OutError)
{
	if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
	{
		OutError = TEXT("Runtime viewport is not available");
		return false;
	}

	FViewport* Viewport = GEngine->GameViewport->Viewport;
	const FIntPoint ViewSize = Viewport->GetSizeXY();
	if (ViewSize.X <= 0 || ViewSize.Y <= 0)
	{
		OutError = TEXT("Runtime viewport has invalid size");
		return false;
	}

	TArray<FColor> Bitmap;
	if (!Viewport->ReadPixels(Bitmap) || Bitmap.Num() == 0)
	{
		OutError = TEXT("Failed to read runtime viewport pixels");
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	const TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!ImageWrapper.IsValid())
	{
		OutError = TEXT("Failed to create PNG image wrapper");
		return false;
	}

	if (!ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), ViewSize.X, ViewSize.Y, ERGBFormat::BGRA, 8))
	{
		OutError = TEXT("Failed to encode raw runtime viewport pixels");
		return false;
	}

	OutPngData = ImageWrapper->GetCompressed(0);
	if (OutPngData.Num() == 0)
	{
		OutError = TEXT("Failed to compress runtime viewport PNG");
		return false;
	}

	OutWidth = ViewSize.X;
	OutHeight = ViewSize.Y;
	return true;
}

FString GetActorNameFromRequest(const TSharedPtr<FJsonObject>& Body, const FHttpServerRequest& Request)
{
	if (Body.IsValid())
	{
		if (Body->HasTypedField<EJson::String>(TEXT("name")))
		{
			return Body->GetStringField(TEXT("name"));
		}
		if (Body->HasTypedField<EJson::String>(TEXT("target")))
		{
			return Body->GetStringField(TEXT("target"));
		}
	}

	if (Request.QueryParams.Contains(TEXT("name")))
	{
		return Request.QueryParams[TEXT("name")];
	}
	if (Request.QueryParams.Contains(TEXT("target")))
	{
		return Request.QueryParams[TEXT("target")];
	}
	return FString();
}

void AddCameraStateToJson(APlayerController* PlayerController, const TSharedPtr<FJsonObject>& OutJson)
{
	if (!OutJson.IsValid())
	{
		return;
	}

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	float CameraFov = 90.0f;

	if (PlayerController && PlayerController->PlayerCameraManager)
	{
		CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
		CameraRotation = PlayerController->PlayerCameraManager->GetCameraRotation();
		CameraFov = PlayerController->PlayerCameraManager->GetFOVAngle();
	}
	else if (PlayerController && PlayerController->GetPawn())
	{
		CameraLocation = PlayerController->GetPawn()->GetActorLocation();
		CameraRotation = PlayerController->GetPawn()->GetActorRotation();
	}

	const TSharedPtr<FJsonObject> LocationObj = MakeShared<FJsonObject>();
	LocationObj->SetNumberField(TEXT("x"), CameraLocation.X);
	LocationObj->SetNumberField(TEXT("y"), CameraLocation.Y);
	LocationObj->SetNumberField(TEXT("z"), CameraLocation.Z);
	OutJson->SetObjectField(TEXT("location"), LocationObj);

	const TSharedPtr<FJsonObject> RotationObj = MakeShared<FJsonObject>();
	RotationObj->SetNumberField(TEXT("pitch"), CameraRotation.Pitch);
	RotationObj->SetNumberField(TEXT("yaw"), CameraRotation.Yaw);
	RotationObj->SetNumberField(TEXT("roll"), CameraRotation.Roll);
	OutJson->SetObjectField(TEXT("rotation"), RotationObj);
	OutJson->SetNumberField(TEXT("fov"), CameraFov);
}
} // namespace

bool FNovaBridgeRuntimeModule::HandleSceneList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	int32 Limit = 500;
	if (Request.QueryParams.Contains(TEXT("limit")))
	{
		Limit = FMath::Clamp(FCString::Atoi(*Request.QueryParams[TEXT("limit")]), 1, 5000);
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Limit]()
	{
		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Actors;
		Actors.Reserve(FMath::Min(Limit, 256));
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			Count++;
			if (Actors.Num() >= Limit)
			{
				continue;
			}
			Actors.Add(MakeShared<FJsonValueObject>(ActorToJsonRuntime(Actor)));
		}

		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("mode"), RuntimeModeName);
		Result->SetStringField(TEXT("level"), World->GetMapName());
		Result->SetNumberField(TEXT("count"), Count);
		Result->SetArrayField(TEXT("actors"), Actors);
		SendJsonResponse(OnComplete, Result);
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandleSceneGet(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	const FString ActorName = GetActorNameFromRequest(Body, Request);
	if (ActorName.IsEmpty())
	{
		SendErrorResponse(OnComplete, TEXT("Missing actor name (name or target)"), 400);
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName]()
	{
		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		AActor* Actor = FindActorByNameRuntime(World, ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		const TSharedPtr<FJsonObject> Result = ActorToJsonRuntime(Actor);
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		SendJsonResponse(OnComplete, Result);
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandleSceneSetProperty(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"), 400);
		return true;
	}

	const FString ActorName = GetActorNameFromRequest(Body, Request);
	const FString PropertyName = Body->HasTypedField<EJson::String>(TEXT("property")) ? Body->GetStringField(TEXT("property")) : FString();
	if (ActorName.IsEmpty() || PropertyName.IsEmpty() || !Body->HasField(TEXT("value")))
	{
		SendErrorResponse(OnComplete, TEXT("Missing required fields: name/target, property, value"), 400);
		return true;
	}

	const TSharedPtr<FJsonValue> Value = Body->Values.FindRef(TEXT("value"));
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, PropertyName, Value]()
	{
		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		AActor* Actor = FindActorByNameRuntime(World, ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		if (PropertyName.Equals(TEXT("location"), ESearchCase::IgnoreCase))
		{
			FVector ParsedLocation = FVector::ZeroVector;
			if (!JsonValueToVector(Value, ParsedLocation))
			{
				SendErrorResponse(OnComplete, TEXT("location must be [x,y,z] or {x,y,z}"), 400);
				return;
			}
			Actor->SetActorLocation(ParsedLocation);
		}
		else if (PropertyName.Equals(TEXT("rotation"), ESearchCase::IgnoreCase))
		{
			FRotator ParsedRotation = FRotator::ZeroRotator;
			if (!JsonValueToRotator(Value, ParsedRotation))
			{
				SendErrorResponse(OnComplete, TEXT("rotation must be [pitch,yaw,roll] or object"), 400);
				return;
			}
			Actor->SetActorRotation(ParsedRotation);
		}
		else if (PropertyName.Equals(TEXT("scale"), ESearchCase::IgnoreCase))
		{
			FVector ParsedScale = FVector(1.0f, 1.0f, 1.0f);
			if (!JsonValueToVector(Value, ParsedScale))
			{
				SendErrorResponse(OnComplete, TEXT("scale must be [x,y,z] or {x,y,z}"), 400);
				return;
			}
			Actor->SetActorScale3D(ParsedScale);
		}
		else
		{
			FString ImportText;
			if (!JsonValueToImportText(Value, ImportText))
			{
				SendErrorResponse(OnComplete, TEXT("Unsupported value type for set-property"), 400);
				return;
			}

			FString PropertyError;
			if (!SetRuntimeActorPropertyValue(Actor, PropertyName, ImportText, PropertyError))
			{
				SendErrorResponse(OnComplete, PropertyError, 400);
				return;
			}
		}

		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("name"), Actor->GetName());
		Result->SetStringField(TEXT("property"), PropertyName);
		SendJsonResponse(OnComplete, Result);
		PushAuditEntry(TEXT("/nova/scene/set-property"), TEXT("scene.set-property"), TEXT("success"),
			FString::Printf(TEXT("Set property '%s' on actor '%s'"), *PropertyName, *Actor->GetName()));
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandleViewportSetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"), 400);
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Body]()
	{
		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!PlayerController)
		{
			SendErrorResponse(OnComplete, TEXT("No runtime player controller available"), 404);
			return;
		}

		if (Body->HasTypedField<EJson::Object>(TEXT("location")))
		{
			const TSharedPtr<FJsonObject> LocationObj = Body->GetObjectField(TEXT("location"));
			const FVector Location(
				LocationObj->GetNumberField(TEXT("x")),
				LocationObj->GetNumberField(TEXT("y")),
				LocationObj->GetNumberField(TEXT("z")));
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				Pawn->SetActorLocation(Location);
			}
			else
			{
				PlayerController->SetInitialLocationAndRotation(Location, PlayerController->GetControlRotation());
			}
		}

		if (Body->HasTypedField<EJson::Object>(TEXT("rotation")))
		{
			const TSharedPtr<FJsonObject> RotationObj = Body->GetObjectField(TEXT("rotation"));
			const FRotator Rotation(
				RotationObj->GetNumberField(TEXT("pitch")),
				RotationObj->GetNumberField(TEXT("yaw")),
				RotationObj->GetNumberField(TEXT("roll")));
			PlayerController->SetControlRotation(Rotation);
			if (APawn* Pawn = PlayerController->GetPawn())
			{
				Pawn->SetActorRotation(Rotation);
			}
		}

		if (Body->HasField(TEXT("fov")) && PlayerController->PlayerCameraManager)
		{
			PlayerController->PlayerCameraManager->SetFOV(static_cast<float>(Body->GetNumberField(TEXT("fov"))));
		}

		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		AddCameraStateToJson(PlayerController, Result);
		SendJsonResponse(OnComplete, Result);
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandleViewportGetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!PlayerController)
		{
			SendErrorResponse(OnComplete, TEXT("No runtime player controller available"), 404);
			return;
		}

		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		AddCameraStateToJson(PlayerController, Result);
		SendJsonResponse(OnComplete, Result);
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandleViewportScreenshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const bool bRawPng = Request.QueryParams.Contains(TEXT("format"))
		&& (Request.QueryParams[TEXT("format")].Equals(TEXT("raw"), ESearchCase::IgnoreCase)
			|| Request.QueryParams[TEXT("format")].Equals(TEXT("png"), ESearchCase::IgnoreCase));
	const bool bInline = Request.QueryParams.Contains(TEXT("inline"))
		|| Request.QueryParams.Contains(TEXT("return_base64"));

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, bRawPng, bInline]()
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray64<uint8> PngData;
		FString CaptureError;
		if (!CaptureRuntimeViewportPng(PngData, Width, Height, CaptureError))
		{
			SendErrorResponse(OnComplete, CaptureError.IsEmpty() ? TEXT("Runtime screenshot capture failed") : CaptureError, 500);
			return;
		}

		if (bRawPng)
		{
			TArray<uint8> RawPng;
			RawPng.Append(PngData.GetData(), static_cast<int32>(PngData.Num()));
			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(MoveTemp(RawPng), TEXT("image/png"));
			Response->Code = EHttpServerResponseCodes::Ok;
			Response->Headers.FindOrAdd(TEXT("X-NovaBridge-Width")).Add(FString::FromInt(Width));
			Response->Headers.FindOrAdd(TEXT("X-NovaBridge-Height")).Add(FString::FromInt(Height));
			AddCorsHeaders(Response);
			OnComplete(MoveTemp(Response));
			return;
		}

		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("format"), TEXT("png"));
		Result->SetNumberField(TEXT("width"), Width);
		Result->SetNumberField(TEXT("height"), Height);
		Result->SetNumberField(TEXT("bytes"), static_cast<double>(PngData.Num()));
		if (bInline)
		{
			Result->SetStringField(TEXT("image"), FBase64::Encode(PngData.GetData(), static_cast<int32>(PngData.Num())));
		}
		SendJsonResponse(OnComplete, Result);
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandleSequencerPlay(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body || !Body->HasTypedField<EJson::String>(TEXT("sequence")))
	{
		SendErrorResponse(OnComplete, TEXT("Missing sequence path"), 400);
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const bool bLoop = Body->HasTypedField<EJson::Boolean>(TEXT("loop")) && Body->GetBoolField(TEXT("loop"));
	const float StartTime = Body->HasField(TEXT("start_time")) ? static_cast<float>(Body->GetNumberField(TEXT("start_time"))) : 0.0f;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, bLoop, StartTime]()
	{
		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
		if (!Sequence)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Sequence not found: %s"), *SequencePath), 404);
			return;
		}

		FMovieSceneSequencePlaybackSettings Settings;
		Settings.LoopCount.Value = bLoop ? -1 : 0;
		ALevelSequenceActor* SequenceActor = nullptr;
		ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
		if (!Player)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create runtime sequence player"), 500);
			return;
		}

		if (StartTime > 0.0f)
		{
			FMovieSceneSequencePlaybackParams Params;
			Params.PositionType = EMovieScenePositionType::Time;
			Params.Time = StartTime;
			Params.UpdateMethod = EUpdatePositionMethod::Jump;
			Player->SetPlaybackPosition(Params);
		}
		Player->Play();

		{
			FRuntimeSequenceState& State = RuntimeSequenceState();
			FScopeLock Lock(&State.Mutex);
			State.Players.Add(SequencePath, Player);
			if (SequenceActor)
			{
				State.Actors.Add(SequencePath, SequenceActor);
			}
		}

		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetBoolField(TEXT("playing"), Player->IsPlaying());
		Result->SetBoolField(TEXT("loop"), bLoop);
		Result->SetNumberField(TEXT("start_time"), StartTime);
		SendJsonResponse(OnComplete, Result);
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandleSequencerStop(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	const FString SequencePath = (Body.IsValid() && Body->HasTypedField<EJson::String>(TEXT("sequence"))) ? Body->GetStringField(TEXT("sequence")) : FString();

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath]()
	{
		int32 StoppedCount = 0;
		{
			FRuntimeSequenceState& State = RuntimeSequenceState();
			FScopeLock Lock(&State.Mutex);
			if (!SequencePath.IsEmpty())
			{
				if (const TWeakObjectPtr<ULevelSequencePlayer>* FoundPlayer = State.Players.Find(SequencePath))
				{
					if (FoundPlayer->IsValid())
					{
						FoundPlayer->Get()->Stop();
						StoppedCount = 1;
					}
				}
				State.Players.Remove(SequencePath);
				if (const TWeakObjectPtr<ALevelSequenceActor>* FoundActor = State.Actors.Find(SequencePath))
				{
					if (FoundActor->IsValid())
					{
						FoundActor->Get()->Destroy();
					}
				}
				State.Actors.Remove(SequencePath);
			}
			else
			{
				for (TPair<FString, TWeakObjectPtr<ULevelSequencePlayer>>& Pair : State.Players)
				{
					if (Pair.Value.IsValid())
					{
						Pair.Value->Stop();
						StoppedCount++;
					}
				}
				for (TPair<FString, TWeakObjectPtr<ALevelSequenceActor>>& Pair : State.Actors)
				{
					if (Pair.Value.IsValid())
					{
						Pair.Value->Destroy();
					}
				}
				State.Players.Empty();
				State.Actors.Empty();
			}
		}

		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetNumberField(TEXT("stopped"), StoppedCount);
		SendJsonResponse(OnComplete, Result);
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandleSequencerInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		TArray<TSharedPtr<FJsonValue>> Entries;
		FRuntimeSequenceState& State = RuntimeSequenceState();
		FScopeLock Lock(&State.Mutex);

		for (auto It = State.Players.CreateIterator(); It; ++It)
		{
			const FString SequencePath = It.Key();
			ULevelSequencePlayer* Player = It.Value().Get();
			if (!Player)
			{
				It.RemoveCurrent();
				State.Actors.Remove(SequencePath);
				continue;
			}

			const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("sequence"), SequencePath);
			Entry->SetBoolField(TEXT("is_playing"), Player->IsPlaying());
			Entry->SetNumberField(TEXT("current_time"), Player->GetCurrentTime().AsSeconds());
			Entry->SetNumberField(TEXT("duration"), Player->GetDuration().AsSeconds());
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetArrayField(TEXT("players"), Entries);
		Result->SetNumberField(TEXT("count"), Entries.Num());
		SendJsonResponse(OnComplete, Result);
	});

	return true;
}

bool FNovaBridgeRuntimeModule::HandlePcgGenerate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body || !Body->HasTypedField<EJson::String>(TEXT("actor_name")))
	{
		SendErrorResponse(OnComplete, TEXT("Missing actor_name"), 400);
		return true;
	}

#if NOVABRIDGE_WITH_PCG
	const FString ActorName = Body->GetStringField(TEXT("actor_name"));
	const bool bForce = !Body->HasField(TEXT("force_regenerate")) || Body->GetBoolField(TEXT("force_regenerate"));
	const int32 Seed = Body->HasField(TEXT("seed")) ? static_cast<int32>(Body->GetNumberField(TEXT("seed"))) : INT32_MIN;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ActorName, bForce, Seed]()
	{
		UWorld* World = ResolveRuntimeWorld();
		if (!World)
		{
			SendErrorResponse(OnComplete, TEXT("No runtime world is available"), 500);
			return;
		}

		AActor* Actor = FindActorByNameRuntime(World, ActorName);
		if (!Actor)
		{
			SendErrorResponse(OnComplete, FString::Printf(TEXT("Actor not found: %s"), *ActorName), 404);
			return;
		}

		UPCGComponent* Component = Actor->FindComponentByClass<UPCGComponent>();
		if (!Component)
		{
			if (APCGVolume* Volume = Cast<APCGVolume>(Actor))
			{
				Component = Volume->PCGComponent;
			}
		}
		if (!Component)
		{
			SendErrorResponse(OnComplete, TEXT("No PCG component on actor"), 404);
			return;
		}

		if (Seed != INT32_MIN)
		{
			Component->Seed = Seed;
		}
		Component->Generate(bForce);

		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("actor"), ActorName);
		Result->SetBoolField(TEXT("generation_triggered"), true);
		Result->SetNumberField(TEXT("seed"), Component->Seed);
		SendJsonResponse(OnComplete, Result);
	});
#else
	SendErrorResponse(OnComplete, TEXT("PCG module is not available in this runtime build"), 501);
#endif
	return true;
}
