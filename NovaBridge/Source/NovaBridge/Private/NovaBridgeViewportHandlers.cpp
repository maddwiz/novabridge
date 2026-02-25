#include "NovaBridgeModule.h"

#include "Async/Async.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Editor.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "ShowFlags.h"
#include "TextureResource.h"

// ============================================================
// Viewport Handlers (Offscreen SceneCapture2D)
// ============================================================

void FNovaBridgeModule::EnsureCaptureSetup()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
		return;

	// Already valid for this world?
	if (CaptureActor.IsValid() && CaptureActor->GetWorld() == World && RenderTarget.IsValid())
	{
		return;
	}

	// If world changed, force rebind.
	if (CaptureActor.IsValid() && CaptureActor->GetWorld() != World)
	{
		CaptureActor.Reset();
	}

	// Reattach to an existing capture actor if one already exists in this level.
	if (!CaptureActor.IsValid())
	{
		for (TActorIterator<ASceneCapture2D> It(World); It; ++It)
		{
			ASceneCapture2D* Existing = *It;
			if (!Existing)
			{
				continue;
			}
			if (Existing->GetActorLabel() == TEXT("NovaBridge_SceneCapture") || Existing->GetName().Contains(TEXT("NovaBridge_SceneCapture")))
			{
				CaptureActor = Existing;
				break;
			}
		}
	}

	if (!RenderTarget.IsValid())
	{
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		RT->InitAutoFormat(CaptureWidth, CaptureHeight);
		RT->UpdateResourceImmediate(true);
		RenderTarget = RT;
	}

	// Spawn scene capture actor
	if (!CaptureActor.IsValid())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (!Capture)
		{
			return;
		}

		Capture->SetActorLabel(TEXT("NovaBridge_SceneCapture"));
		Capture->SetActorHiddenInGame(true);
		CaptureActor = Capture;
	}

	ASceneCapture2D* Capture = CaptureActor.Get();
	UTextureRenderTarget2D* RT = RenderTarget.Get();
	if (Capture && RT)
	{
		USceneCaptureComponent2D* CaptureComp = Capture->GetCaptureComponent2D();
		CaptureComp->TextureTarget = RT;
		CaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CaptureComp->bCaptureEveryFrame = false;
		CaptureComp->bCaptureOnMovement = false;
		CaptureComp->FOVAngle = CameraFOV;

		// Position at default camera location
		Capture->SetActorLocation(CameraLocation);
		Capture->SetActorRotation(CameraRotation);

		UE_LOG(LogNovaBridge, Log, TEXT("Scene capture ready: %dx%d"), CaptureWidth, CaptureHeight);
	}
}

void FNovaBridgeModule::CleanupCapture()
{
	if (CaptureActor.IsValid())
	{
		CaptureActor->Destroy();
		CaptureActor.Reset();
	}
	RenderTarget.Reset();
}

void FNovaBridgeModule::EnsureStreamCaptureSetup()
{
	if (StreamCaptureActor.IsValid() && StreamRenderTarget.IsValid())
	{
		return;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
	RT->InitAutoFormat(StreamWidth, StreamHeight);
	RT->UpdateResourceImmediate(true);
	StreamRenderTarget = RT;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = FName(TEXT("NovaBridge_StreamCapture"));
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASceneCapture2D* Capture = World->SpawnActor<ASceneCapture2D>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (Capture)
	{
		Capture->SetActorLabel(TEXT("NovaBridge_StreamCapture"));
		Capture->SetActorHiddenInGame(true);
		USceneCaptureComponent2D* CaptureComp = Capture->GetCaptureComponent2D();
		CaptureComp->TextureTarget = RT;
		CaptureComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		CaptureComp->bCaptureEveryFrame = false;
		CaptureComp->bCaptureOnMovement = false;
		CaptureComp->FOVAngle = CameraFOV;
		Capture->SetActorLocation(CameraLocation);
		Capture->SetActorRotation(CameraRotation);
		StreamCaptureActor = Capture;
	}
}

void FNovaBridgeModule::CleanupStreamCapture()
{
	if (StreamCaptureActor.IsValid())
	{
		StreamCaptureActor->Destroy();
		StreamCaptureActor.Reset();
	}
	StreamRenderTarget.Reset();
}

bool FNovaBridgeModule::HandleViewportScreenshot(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Parse optional width/height from query params
	int32 ReqWidth = 0, ReqHeight = 0;
	bool bRawPng = false;
	if (Request.QueryParams.Contains(TEXT("width")))
	{
		ReqWidth = FCString::Atoi(*Request.QueryParams[TEXT("width")]);
	}
	if (Request.QueryParams.Contains(TEXT("height")))
	{
		ReqHeight = FCString::Atoi(*Request.QueryParams[TEXT("height")]);
	}
	if (Request.QueryParams.Contains(TEXT("format")))
	{
		const FString Format = Request.QueryParams[TEXT("format")].ToLower();
		bRawPng = (Format == TEXT("raw") || Format == TEXT("png"));
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, ReqWidth, ReqHeight, bRawPng]()
	{
		if (!GEditor)
		{
			SendErrorResponse(OnComplete, TEXT("No editor"), 500);
			return;
		}

		// Resize render target if requested
		if (ReqWidth > 0 && ReqHeight > 0 && (ReqWidth != CaptureWidth || ReqHeight != CaptureHeight))
		{
			CaptureWidth = FMath::Clamp(ReqWidth, 64, 3840);
			CaptureHeight = FMath::Clamp(ReqHeight, 64, 2160);
			CleanupCapture(); // Force re-creation at new size
		}

		EnsureCaptureSetup();

		if (!CaptureActor.IsValid() || !RenderTarget.IsValid())
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create scene capture"), 500);
			return;
		}

		// Update capture component position to current camera state
		USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
		CaptureActor->SetActorLocation(CameraLocation);
		CaptureActor->SetActorRotation(CameraRotation);
		CaptureComp->FOVAngle = CameraFOV;

		// Capture the scene
		CaptureComp->CaptureScene();

		// Read pixels from render target
		FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
		if (!RTResource)
		{
			SendErrorResponse(OnComplete, TEXT("No render target resource"), 500);
			return;
		}

		TArray<FColor> Bitmap;
		bool bSuccess = RTResource->ReadPixels(Bitmap);
		if (!bSuccess || Bitmap.Num() == 0)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to read render target pixels"), 500);
			return;
		}

		int32 Width = CaptureWidth;
		int32 Height = CaptureHeight;

		// Encode as PNG
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8);

		TArray64<uint8> PngData = ImageWrapper->GetCompressed(0);
		if (PngData.Num() > 0)
		{
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

			FString Base64 = FBase64::Encode(PngData.GetData(), PngData.Num());

			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetStringField(TEXT("image"), Base64);
			Result->SetNumberField(TEXT("width"), Width);
			Result->SetNumberField(TEXT("height"), Height);
			Result->SetStringField(TEXT("format"), TEXT("png"));
			SendJsonResponse(OnComplete, Result);
		}
		else
		{
			SendErrorResponse(OnComplete, TEXT("Failed to encode PNG"), 500);
		}
	});
	return true;
}

bool FNovaBridgeModule::HandleViewportSetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Body]()
	{
		TArray<FString> UnknownShowFlags;

		if (Body->HasField(TEXT("location")))
		{
			TSharedPtr<FJsonObject> Loc = Body->GetObjectField(TEXT("location"));
			CameraLocation = FVector(
				Loc->GetNumberField(TEXT("x")),
				Loc->GetNumberField(TEXT("y")),
				Loc->GetNumberField(TEXT("z"))
			);
		}

		if (Body->HasField(TEXT("rotation")))
		{
			TSharedPtr<FJsonObject> Rot = Body->GetObjectField(TEXT("rotation"));
			CameraRotation = FRotator(
				Rot->GetNumberField(TEXT("pitch")),
				Rot->GetNumberField(TEXT("yaw")),
				Rot->GetNumberField(TEXT("roll"))
			);
		}

		if (Body->HasField(TEXT("fov")))
		{
			CameraFOV = Body->GetNumberField(TEXT("fov"));
		}

		if (Body->HasTypedField<EJson::Object>(TEXT("show_flags")))
		{
			EnsureCaptureSetup();
			if (CaptureActor.IsValid())
			{
				USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
				TSharedPtr<FJsonObject> ShowFlagsObj = Body->GetObjectField(TEXT("show_flags"));
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ShowFlagsObj->Values)
				{
					bool bEnabled = false;
					if (!Pair.Value.IsValid() || !Pair.Value->TryGetBool(bEnabled))
					{
						UnknownShowFlags.Add(Pair.Key);
						continue;
					}

					const int32 FlagIndex = FEngineShowFlags::FindIndexByName(*Pair.Key);
					if (FlagIndex < 0)
					{
						UnknownShowFlags.Add(Pair.Key);
						continue;
					}

					CaptureComp->ShowFlags.SetSingleFlag(static_cast<uint32>(FlagIndex), bEnabled);
				}
			}
		}

		// Update capture actor position if it exists
		if (CaptureActor.IsValid())
		{
			CaptureActor->SetActorLocation(CameraLocation);
			CaptureActor->SetActorRotation(CameraRotation);
			USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
			CaptureComp->FOVAngle = CameraFOV;
		}

		// Return current camera state
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetStringField(TEXT("status"), TEXT("ok"));

		TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
		LocObj->SetNumberField(TEXT("x"), CameraLocation.X);
		LocObj->SetNumberField(TEXT("y"), CameraLocation.Y);
		LocObj->SetNumberField(TEXT("z"), CameraLocation.Z);
		Result->SetObjectField(TEXT("location"), LocObj);

		TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject);
		RotObj->SetNumberField(TEXT("pitch"), CameraRotation.Pitch);
		RotObj->SetNumberField(TEXT("yaw"), CameraRotation.Yaw);
		RotObj->SetNumberField(TEXT("roll"), CameraRotation.Roll);
		Result->SetObjectField(TEXT("rotation"), RotObj);

		Result->SetNumberField(TEXT("fov"), CameraFOV);
		if (UnknownShowFlags.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Unknown;
			for (const FString& FlagName : UnknownShowFlags)
			{
				Unknown.Add(MakeShareable(new FJsonValueString(FlagName)));
			}
			Result->SetArrayField(TEXT("unknown_show_flags"), Unknown);
		}
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}

bool FNovaBridgeModule::HandleViewportGetCamera(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	AsyncTask(ENamedThreads::GameThread, [this, OnComplete]()
	{
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

		TSharedPtr<FJsonObject> LocObj = MakeShareable(new FJsonObject);
		LocObj->SetNumberField(TEXT("x"), CameraLocation.X);
		LocObj->SetNumberField(TEXT("y"), CameraLocation.Y);
		LocObj->SetNumberField(TEXT("z"), CameraLocation.Z);
		Result->SetObjectField(TEXT("location"), LocObj);

		TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject);
		RotObj->SetNumberField(TEXT("pitch"), CameraRotation.Pitch);
		RotObj->SetNumberField(TEXT("yaw"), CameraRotation.Yaw);
		RotObj->SetNumberField(TEXT("roll"), CameraRotation.Roll);
		Result->SetObjectField(TEXT("rotation"), RotObj);

		Result->SetNumberField(TEXT("fov"), CameraFOV);
		Result->SetNumberField(TEXT("width"), CaptureWidth);
		Result->SetNumberField(TEXT("height"), CaptureHeight);
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}
