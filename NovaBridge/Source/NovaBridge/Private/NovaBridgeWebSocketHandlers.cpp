#include "NovaBridgeModule.h"
#include "NovaBridgeEditorInternals.h"
#include "NovaBridgeHttpUtils.h"

#include "Async/Async.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Containers/StringConv.h"
#include "Dom/JsonValue.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"

#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
#include "INetworkingWebSocket.h"
#include "IWebSocketNetworkingModule.h"
#include "IWebSocketServer.h"
#include "WebSocketNetworkingDelegates.h"
#endif

using NovaBridgeCore::MakeJsonStringArray;
using NovaBridgeCore::NormalizeEventType;

namespace
{
bool IsSupportedEventType(const FString& EventType)
{
	return SupportedEventTypes().Contains(EventType);
}

FString SerializeJsonObject(const TSharedPtr<FJsonObject>& JsonObj)
{
	FString Serialized;
	if (!JsonObj.IsValid())
	{
		return Serialized;
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
	return Serialized;
}

void SendSocketJsonMessage(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& JsonObj)
{
	if (!Socket || !JsonObj.IsValid())
	{
		return;
	}

	const FString Serialized = SerializeJsonObject(JsonObj);
	if (Serialized.IsEmpty())
	{
		return;
	}

	const FTCHARToUTF8 Utf8Payload(*Serialized);
	const uint8* Data = reinterpret_cast<const uint8*>(Utf8Payload.Get());
	uint8* MutableData = const_cast<uint8*>(Data);
	Socket->Send(MutableData, Utf8Payload.Length(), false);
}

bool ParseEventSubscriptionPayload(const FString& Message, TSet<FString>& OutTypes, bool& bOutEnableFilter, FString& OutError)
{
	OutTypes.Reset();
	bOutEnableFilter = false;

	TSharedPtr<FJsonObject> JsonObj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		OutError = TEXT("Invalid subscription message JSON");
		return false;
	}

	FString Action = JsonObj->HasTypedField<EJson::String>(TEXT("action"))
		? NormalizeEventType(JsonObj->GetStringField(TEXT("action")))
		: TEXT("subscribe");
	if (Action.IsEmpty())
	{
		Action = TEXT("subscribe");
	}

	if (Action == TEXT("clear") || Action == TEXT("all") || Action == TEXT("subscribe_all") || Action == TEXT("reset"))
	{
		return true;
	}

	if (Action != TEXT("subscribe"))
	{
		OutError = FString::Printf(TEXT("Unsupported subscription action: %s"), *Action);
		return false;
	}

	if (!JsonObj->HasTypedField<EJson::Array>(TEXT("types")))
	{
		OutError = TEXT("Missing 'types' array for subscribe action");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>& TypeValues = JsonObj->GetArrayField(TEXT("types"));
	for (const TSharedPtr<FJsonValue>& TypeValue : TypeValues)
	{
		if (!TypeValue.IsValid() || TypeValue->Type != EJson::String)
		{
			OutError = TEXT("Subscription 'types' entries must be strings");
			return false;
		}

		const FString Type = NormalizeEventType(TypeValue->AsString());
		if (Type.IsEmpty())
		{
			continue;
		}
		if (!IsSupportedEventType(Type))
		{
			OutError = FString::Printf(TEXT("Unsupported event type: %s"), *Type);
			return false;
		}
		OutTypes.Add(Type);
	}

	bOutEnableFilter = OutTypes.Num() > 0;
	return true;
}
} // namespace

void FNovaBridgeModule::StartWebSocketServer()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (WsServer.IsValid())
	{
		return;
	}

	int32 ParsedWsPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeWsPort="), ParsedWsPort))
	{
		if (ParsedWsPort > 0 && ParsedWsPort <= 65535)
		{
			WsPort = static_cast<uint32>(ParsedWsPort);
		}
	}

	FWebSocketClientConnectedCallBack ConnectedCallback;
	ConnectedCallback.BindLambda([this](INetworkingWebSocket* Socket)
	{
		if (!Socket)
		{
			return;
		}

		FWsClient Client;
		Client.Socket = Socket;
		Client.Id = FGuid::NewGuid();
		WsClients.Add(MoveTemp(Client));

		FWebSocketPacketReceivedCallBack ReceiveCallback;
		ReceiveCallback.BindLambda([](void* Data, int32 Size)
		{
			(void)Data;
			(void)Size;
		});
		Socket->SetReceiveCallBack(ReceiveCallback);

		FWebSocketInfoCallBack CloseCallback;
		CloseCallback.BindLambda([this, Socket]()
		{
			const int32 Index = WsClients.IndexOfByPredicate([Socket](const FWsClient& Client)
			{
				return Client.Socket == Socket;
			});
			if (Index != INDEX_NONE)
			{
				if (WsClients[Index].Socket)
				{
					delete WsClients[Index].Socket;
					WsClients[Index].Socket = nullptr;
				}
				WsClients.RemoveAtSwap(Index);
			}

			if (WsClients.Num() == 0)
			{
				bStreamActive = false;
				StopStreamTicker();
			}
		});
		Socket->SetSocketClosedCallBack(CloseCallback);

		// Auto-start stream when first client connects.
		if (!bStreamActive)
		{
			bStreamActive = true;
		}
		StartStreamTicker();

		UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge stream client connected (%d total)"), WsClients.Num());
	});

	IWebSocketNetworkingModule* WsModule = FModuleManager::Get().LoadModulePtr<IWebSocketNetworkingModule>(TEXT("WebSocketNetworking"));
	if (!WsModule)
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("WebSocketNetworking module not available; stream WebSocket server disabled"));
		return;
	}

	WsServer = WsModule->CreateServer();
	if (!WsServer.IsValid() || !WsServer->Init(WsPort, ConnectedCallback))
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("NovaBridge WebSocket server failed to initialize on port %d"), WsPort);
		WsServer.Reset();
		return;
	}

	WsServerTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
	{
		(void)DeltaTime;
		if (WsServer.IsValid())
		{
			WsServer->Tick();
		}
		return true;
	}));

	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge WebSocket stream server started on port %d"), WsPort);
#else
	UE_LOG(LogNovaBridge, Warning, TEXT("WebSocketNetworking module not available; stream WebSocket server disabled"));
#endif
}

void FNovaBridgeModule::StopWebSocketServer()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	StopStreamTicker();
	bStreamActive = false;

	if (WsServerTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(WsServerTickHandle);
		WsServerTickHandle.Reset();
	}

	for (FWsClient& Client : WsClients)
	{
		if (Client.Socket)
		{
			delete Client.Socket;
			Client.Socket = nullptr;
		}
	}
	WsClients.Empty();
	WsServer.Reset();
#endif
}

void FNovaBridgeModule::StartEventWebSocketServer()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (EventWsServer.IsValid())
	{
		return;
	}

	int32 ParsedEventWsPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NovaBridgeEventsPort="), ParsedEventWsPort))
	{
		if (ParsedEventWsPort > 0 && ParsedEventWsPort <= 65535)
		{
			EventWsPort = static_cast<uint32>(ParsedEventWsPort);
		}
	}
	RegisterEditorCapabilities(EventWsPort);

	FWebSocketClientConnectedCallBack ConnectedCallback;
	ConnectedCallback.BindLambda([this](INetworkingWebSocket* Socket)
	{
		if (!Socket)
		{
			return;
		}

		FWsClient Client;
		Client.Socket = Socket;
		Client.Id = FGuid::NewGuid();
		Client.bSubscriptionConfirmed = false;
		Client.bEventTypeFilterEnabled = false;
		EventWsClients.Add(MoveTemp(Client));

		FWebSocketPacketReceivedCallBack ReceiveCallback;
		ReceiveCallback.BindLambda([this, Socket](void* Data, int32 Size)
		{
			if (!Data || Size <= 0)
			{
				return;
			}

			const FUTF8ToTCHAR Converted(static_cast<const ANSICHAR*>(Data), Size);
			const FString Message(Converted.Length(), Converted.Get());
			TSet<FString> RequestedTypes;
			bool bEnableFilter = false;
			FString ParseError;
			if (!ParseEventSubscriptionPayload(Message, RequestedTypes, bEnableFilter, ParseError))
			{
				TSharedPtr<FJsonObject> ErrorReply = MakeShared<FJsonObject>();
				ErrorReply->SetStringField(TEXT("type"), TEXT("subscription"));
				ErrorReply->SetStringField(TEXT("status"), TEXT("error"));
				ErrorReply->SetStringField(TEXT("message"), ParseError);
				ErrorReply->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedEventTypes()));
				SendSocketJsonMessage(Socket, ErrorReply);
				return;
			}

			const int32 ClientIndex = EventWsClients.IndexOfByPredicate([Socket](const FWsClient& InClient)
			{
				return InClient.Socket == Socket;
			});
			if (ClientIndex == INDEX_NONE)
			{
				return;
			}

			EventWsClients[ClientIndex].bEventTypeFilterEnabled = bEnableFilter;
			EventWsClients[ClientIndex].EventTypes = MoveTemp(RequestedTypes);
			EventWsClients[ClientIndex].bSubscriptionConfirmed = true;

			TSharedPtr<FJsonObject> AckReply = MakeShared<FJsonObject>();
			AckReply->SetStringField(TEXT("type"), TEXT("subscription"));
			AckReply->SetStringField(TEXT("status"), TEXT("ok"));
			AckReply->SetBoolField(TEXT("subscription_confirmed"), true);
			AckReply->SetBoolField(TEXT("filter_enabled"), EventWsClients[ClientIndex].bEventTypeFilterEnabled);
			AckReply->SetArrayField(TEXT("types"), MakeJsonStringArray(EventWsClients[ClientIndex].EventTypes.Array()));
			SendSocketJsonMessage(Socket, AckReply);
		});
		Socket->SetReceiveCallBack(ReceiveCallback);

		FWebSocketInfoCallBack CloseCallback;
		CloseCallback.BindLambda([this, Socket]()
		{
			const int32 Index = EventWsClients.IndexOfByPredicate([Socket](const FWsClient& Client)
			{
				return Client.Socket == Socket;
			});
			if (Index != INDEX_NONE)
			{
				if (EventWsClients[Index].Socket)
				{
					delete EventWsClients[Index].Socket;
					EventWsClients[Index].Socket = nullptr;
				}
				EventWsClients.RemoveAtSwap(Index);
			}
		});
		Socket->SetSocketClosedCallBack(CloseCallback);

		TSharedPtr<FJsonObject> WelcomeReply = MakeShared<FJsonObject>();
		WelcomeReply->SetStringField(TEXT("type"), TEXT("subscription"));
		WelcomeReply->SetStringField(TEXT("status"), TEXT("ready"));
		WelcomeReply->SetBoolField(TEXT("subscription_confirmed"), false);
		WelcomeReply->SetBoolField(TEXT("events_paused_until_subscribe"), true);
		WelcomeReply->SetBoolField(TEXT("filter_enabled"), false);
		WelcomeReply->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedEventTypes()));
		WelcomeReply->SetStringField(TEXT("hint"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
		SendSocketJsonMessage(Socket, WelcomeReply);

		UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge events client connected (%d total)"), EventWsClients.Num());
	});

	IWebSocketNetworkingModule* WsModule = FModuleManager::Get().LoadModulePtr<IWebSocketNetworkingModule>(TEXT("WebSocketNetworking"));
	if (!WsModule)
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("WebSocketNetworking module not available; events WebSocket server disabled"));
		return;
	}

	EventWsServer = WsModule->CreateServer();
	if (!EventWsServer.IsValid() || !EventWsServer->Init(EventWsPort, ConnectedCallback))
	{
		UE_LOG(LogNovaBridge, Warning, TEXT("NovaBridge events WebSocket server failed to initialize on port %d"), EventWsPort);
		EventWsServer.Reset();
		return;
	}

	EventWsServerTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
	{
		(void)DeltaTime;
		if (EventWsServer.IsValid())
		{
			EventWsServer->Tick();
		}
		PumpEventSocketQueue();
		return true;
	}));

	UE_LOG(LogNovaBridge, Log, TEXT("NovaBridge events WebSocket server started on port %d"), EventWsPort);
#else
	UE_LOG(LogNovaBridge, Warning, TEXT("WebSocketNetworking module not available; events WebSocket server disabled"));
#endif
}

void FNovaBridgeModule::StopEventWebSocketServer()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (EventWsServerTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EventWsServerTickHandle);
		EventWsServerTickHandle.Reset();
	}

	for (FWsClient& Client : EventWsClients)
	{
		if (Client.Socket)
		{
			delete Client.Socket;
			Client.Socket = nullptr;
		}
	}
	EventWsClients.Empty();
	EventWsServer.Reset();
#endif
}

void FNovaBridgeModule::StartStreamTicker()
{
	if (!bStreamActive || WsClients.Num() == 0 || StreamTickHandle.IsValid())
	{
		return;
	}

	LastStreamFrameTime = 0.0;
	StreamTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
	{
		(void)DeltaTime;
		StreamTick();
		return true;
	}), 0.0f);
}

void FNovaBridgeModule::StopStreamTicker()
{
	if (StreamTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StreamTickHandle);
		StreamTickHandle.Reset();
	}
}

void FNovaBridgeModule::StreamTick()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (!bStreamActive || WsClients.Num() == 0)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const int32 SafeFps = FMath::Max(1, StreamFps);
	if (Now - LastStreamFrameTime < (1.0 / static_cast<double>(SafeFps)))
	{
		return;
	}
	LastStreamFrameTime = Now;

	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		if (!bStreamActive || WsClients.Num() == 0)
		{
			return;
		}

		EnsureStreamCaptureSetup();
		if (!StreamCaptureActor.IsValid() || !StreamRenderTarget.IsValid())
		{
			return;
		}

		USceneCaptureComponent2D* CaptureComp = StreamCaptureActor->GetCaptureComponent2D();
		StreamCaptureActor->SetActorLocation(CameraLocation);
		StreamCaptureActor->SetActorRotation(CameraRotation);
		CaptureComp->FOVAngle = CameraFOV;
		CaptureComp->CaptureScene();

		FTextureRenderTargetResource* RTResource = StreamRenderTarget->GameThread_GetRenderTargetResource();
		if (!RTResource)
		{
			return;
		}

		TArray<FColor> Bitmap;
		if (!RTResource->ReadPixels(Bitmap) || Bitmap.Num() == 0)
		{
			return;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
		ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), StreamWidth, StreamHeight, ERGBFormat::BGRA, 8);
		TArray64<uint8> Encoded = ImageWrapper->GetCompressed(FMath::Clamp(StreamQuality, 1, 100));
		if (Encoded.Num() == 0)
		{
			return;
		}

		TArray<uint8> Payload;
		Payload.Append(Encoded.GetData(), static_cast<int32>(Encoded.Num()));
		for (int32 Idx = WsClients.Num() - 1; Idx >= 0; --Idx)
		{
			if (!WsClients[Idx].Socket)
			{
				WsClients.RemoveAtSwap(Idx);
				continue;
			}
			WsClients[Idx].Socket->Send(Payload.GetData(), Payload.Num(), false);
		}
	});
#endif
}
