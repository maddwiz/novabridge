#include "NovaBridgeModule.h"

#include "Async/Async.h"

bool FNovaBridgeModule::HandleStreamStart(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	bStreamActive = true;
	StartStreamTicker();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetBoolField(TEXT("active"), bStreamActive && StreamTickHandle.IsValid());
	Result->SetNumberField(TEXT("clients"), WsClients.Num());
	Result->SetNumberField(TEXT("fps"), StreamFps);
	Result->SetNumberField(TEXT("width"), StreamWidth);
	Result->SetNumberField(TEXT("height"), StreamHeight);
	Result->SetNumberField(TEXT("quality"), StreamQuality);
	Result->SetNumberField(TEXT("ws_port"), WsPort);
	Result->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), WsPort));
#if !NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	Result->SetStringField(TEXT("warning"), TEXT("WebSocketNetworking module unavailable in this UE build."));
#endif
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleStreamStop(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	bStreamActive = false;
	StopStreamTicker();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetBoolField(TEXT("active"), false);
	Result->SetNumberField(TEXT("clients"), WsClients.Num());
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleStreamConfig(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body)
	{
		SendErrorResponse(OnComplete, TEXT("Invalid JSON body"));
		return true;
	}

	double Value = 0.0;
	bool bResized = false;

	if (Body->TryGetNumberField(TEXT("fps"), Value))
	{
		StreamFps = FMath::Clamp(static_cast<int32>(Value), 1, 30);
	}
	if (Body->TryGetNumberField(TEXT("width"), Value))
	{
		const int32 NewWidth = FMath::Clamp(static_cast<int32>(Value), 64, 1920);
		bResized = bResized || (NewWidth != StreamWidth);
		StreamWidth = NewWidth;
	}
	if (Body->TryGetNumberField(TEXT("height"), Value))
	{
		const int32 NewHeight = FMath::Clamp(static_cast<int32>(Value), 64, 1080);
		bResized = bResized || (NewHeight != StreamHeight);
		StreamHeight = NewHeight;
	}
	if (Body->TryGetNumberField(TEXT("quality"), Value))
	{
		StreamQuality = FMath::Clamp(static_cast<int32>(Value), 1, 100);
	}

	if (bResized)
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			CleanupStreamCapture();
		});
	}

	if (bStreamActive)
	{
		StopStreamTicker();
		StartStreamTicker();
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetBoolField(TEXT("active"), bStreamActive && StreamTickHandle.IsValid());
	Result->SetNumberField(TEXT("clients"), WsClients.Num());
	Result->SetNumberField(TEXT("fps"), StreamFps);
	Result->SetNumberField(TEXT("width"), StreamWidth);
	Result->SetNumberField(TEXT("height"), StreamHeight);
	Result->SetNumberField(TEXT("quality"), StreamQuality);
	Result->SetNumberField(TEXT("ws_port"), WsPort);
	Result->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), WsPort));
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleStreamStatus(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("active"), bStreamActive && StreamTickHandle.IsValid());
	Result->SetNumberField(TEXT("clients"), WsClients.Num());
	Result->SetNumberField(TEXT("fps"), StreamFps);
	Result->SetNumberField(TEXT("width"), StreamWidth);
	Result->SetNumberField(TEXT("height"), StreamHeight);
	Result->SetNumberField(TEXT("quality"), StreamQuality);
	Result->SetNumberField(TEXT("ws_port"), WsPort);
	Result->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), WsPort));
	SendJsonResponse(OnComplete, Result);
	return true;
}
