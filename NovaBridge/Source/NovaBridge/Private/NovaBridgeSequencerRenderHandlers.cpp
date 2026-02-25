#include "NovaBridgeModule.h"
#include "NovaBridgeEditorInternals.h"

#include "Async/Async.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Editor.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"

bool FNovaBridgeModule::HandleSequencerRender(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	const FString SequencePath = Body->GetStringField(TEXT("sequence"));
	const FString OutputPath = Body->HasField(TEXT("output_path"))
		? Body->GetStringField(TEXT("output_path"))
		: (FPaths::ProjectSavedDir() / TEXT("NovaBridgeRenders") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
	const int32 Fps = Body->HasField(TEXT("fps")) ? FMath::Clamp(static_cast<int32>(Body->GetNumberField(TEXT("fps"))), 1, 60) : 24;
	const float Duration = Body->HasField(TEXT("duration_seconds")) ? static_cast<float>(Body->GetNumberField(TEXT("duration_seconds"))) : 5.0f;

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, SequencePath, OutputPath, Fps, Duration]()
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

		IFileManager::Get().MakeDirectory(*OutputPath, true);

		FMovieSceneSequencePlaybackSettings Settings;
		ALevelSequenceActor* SequenceActor = nullptr;
		ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, Settings, SequenceActor);
		if (!Player)
		{
			SendErrorResponse(OnComplete, TEXT("Failed to create sequence player"), 500);
			return;
		}

		const int32 FrameCount = FMath::Clamp(FMath::CeilToInt(Duration * Fps), 1, 900);
		EnsureCaptureSetup();
		if (!CaptureActor.IsValid() || !RenderTarget.IsValid())
		{
			SendErrorResponse(OnComplete, TEXT("Failed to initialize capture for render"), 500);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Frames;
		for (int32 FrameIdx = 0; FrameIdx < FrameCount; ++FrameIdx)
		{
			const float TimeSeconds = static_cast<float>(FrameIdx) / static_cast<float>(Fps);
			NovaBridgeSetPlaybackTime(Player, TimeSeconds, false);

			USceneCaptureComponent2D* CaptureComp = CaptureActor->GetCaptureComponent2D();
			CaptureActor->SetActorLocation(CameraLocation);
			CaptureActor->SetActorRotation(CameraRotation);
			CaptureComp->FOVAngle = CameraFOV;
			CaptureComp->CaptureScene();

			FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
			if (!RTResource)
			{
				continue;
			}

			TArray<FColor> Bitmap;
			if (!RTResource->ReadPixels(Bitmap) || Bitmap.Num() == 0)
			{
				continue;
			}

			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), CaptureWidth, CaptureHeight, ERGBFormat::BGRA, 8);
			TArray64<uint8> PngData = ImageWrapper->GetCompressed(0);

			const FString FramePath = OutputPath / FString::Printf(TEXT("frame_%05d.png"), FrameIdx);
			TArray<uint8> PngData32;
			PngData32.Append(PngData.GetData(), static_cast<int32>(PngData.Num()));
			if (FFileHelper::SaveArrayToFile(PngData32, *FramePath))
			{
				Frames.Add(MakeShared<FJsonValueString>(FramePath));
			}
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("sequence"), SequencePath);
		Result->SetStringField(TEXT("output_path"), OutputPath);
		Result->SetStringField(TEXT("format"), TEXT("png-sequence"));
		Result->SetNumberField(TEXT("fps"), Fps);
		Result->SetNumberField(TEXT("frame_count"), Frames.Num());
		Result->SetArrayField(TEXT("frames"), Frames);
		Result->SetStringField(TEXT("note"), TEXT("Rendered as PNG sequence. Use ffmpeg externally for MP4 encoding."));
		SendJsonResponse(OnComplete, Result);
	});
	return true;
}
