#include "NovaBridgeRuntimeModule.h"

#include "NovaBridgeHttpUtils.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
#include "INetworkingWebSocket.h"
#include "IWebSocketNetworkingModule.h"
#include "IWebSocketServer.h"
#include "WebSocketNetworkingDelegates.h"
#endif

namespace
{
using NovaBridgeCore::MakeJsonStringArray;
using NovaBridgeCore::NormalizeEventType;
using NovaBridgeCore::ParseEventTypeFilter;

const TArray<FString>& SupportedRuntimeEventTypes()
{
	static const TArray<FString> Types =
	{
		TEXT("audit"),
		TEXT("spawn"),
		TEXT("delete"),
		TEXT("plan_step"),
		TEXT("plan_complete"),
		TEXT("error")
	};
	return Types;
}

bool IsSupportedRuntimeEventType(const FString& EventType)
{
	return SupportedRuntimeEventTypes().Contains(EventType);
}

FString SerializeJsonObject(const TSharedPtr<FJsonObject>& JsonObj)
{
	FString Serialized;
	if (!JsonObj.IsValid())
	{
		return Serialized;
	}

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
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

bool ParseRuntimeSubscriptionPayload(const FString& Message, TSet<FString>& OutTypes, bool& bOutEnableFilter, FString& OutError)
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
		if (!IsSupportedRuntimeEventType(Type))
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

void FNovaBridgeRuntimeModule::StartEventWebSocketServer()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (EventWsServer.IsValid())
	{
		return;
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
			if (!ParseRuntimeSubscriptionPayload(Message, RequestedTypes, bEnableFilter, ParseError))
			{
				const TSharedPtr<FJsonObject> ErrorReply = MakeShared<FJsonObject>();
				ErrorReply->SetStringField(TEXT("type"), TEXT("subscription"));
				ErrorReply->SetStringField(TEXT("status"), TEXT("error"));
				ErrorReply->SetStringField(TEXT("message"), ParseError);
				ErrorReply->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedRuntimeEventTypes()));
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

			const TSharedPtr<FJsonObject> AckReply = MakeShared<FJsonObject>();
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
			const int32 Index = EventWsClients.IndexOfByPredicate([Socket](const FWsClient& InClient)
			{
				return InClient.Socket == Socket;
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

		const TSharedPtr<FJsonObject> WelcomeReply = MakeShared<FJsonObject>();
		WelcomeReply->SetStringField(TEXT("type"), TEXT("subscription"));
		WelcomeReply->SetStringField(TEXT("status"), TEXT("ready"));
		WelcomeReply->SetBoolField(TEXT("subscription_confirmed"), false);
		WelcomeReply->SetBoolField(TEXT("events_paused_until_subscribe"), true);
		WelcomeReply->SetBoolField(TEXT("filter_enabled"), false);
		WelcomeReply->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedRuntimeEventTypes()));
		WelcomeReply->SetStringField(TEXT("hint"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
		SendSocketJsonMessage(Socket, WelcomeReply);

		UE_LOG(LogNovaBridgeRuntime, Log, TEXT("NovaBridgeRuntime events client connected (%d total)"), EventWsClients.Num());
	});

	IWebSocketNetworkingModule* WsModule = FModuleManager::Get().LoadModulePtr<IWebSocketNetworkingModule>(TEXT("WebSocketNetworking"));
	if (!WsModule)
	{
		UE_LOG(LogNovaBridgeRuntime, Warning, TEXT("WebSocketNetworking module not available; runtime events WebSocket server disabled"));
		return;
	}

	EventWsServer = WsModule->CreateServer();
	if (!EventWsServer.IsValid() || !EventWsServer->Init(EventWsPort, ConnectedCallback))
	{
		UE_LOG(LogNovaBridgeRuntime, Warning, TEXT("Runtime events WebSocket server failed to initialize on port %d"), EventWsPort);
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

	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("Runtime events WebSocket server started on port %d"), EventWsPort);
#else
	UE_LOG(LogNovaBridgeRuntime, Warning, TEXT("WebSocketNetworking module not available; runtime events WebSocket server disabled"));
#endif
}

void FNovaBridgeRuntimeModule::StopEventWebSocketServer()
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

	FScopeLock EventLock(&RuntimeEventQueueMutex);
	RuntimePendingEventPayloads.Empty();
	RuntimePendingEventTypes.Empty();
}

void FNovaBridgeRuntimeModule::PumpEventSocketQueue()
{
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	if (EventWsClients.Num() == 0)
	{
		return;
	}

	TArray<FString> PendingPayloads;
	TArray<FString> PendingTypes;
	{
		FScopeLock EventLock(&RuntimeEventQueueMutex);
		if (RuntimePendingEventPayloads.Num() == 0)
		{
			return;
		}
		PendingPayloads = MoveTemp(RuntimePendingEventPayloads);
		PendingTypes = MoveTemp(RuntimePendingEventTypes);
		RuntimePendingEventPayloads.Reset();
		RuntimePendingEventTypes.Reset();
	}
	if (PendingTypes.Num() != PendingPayloads.Num())
	{
		PendingTypes.Init(TEXT("audit"), PendingPayloads.Num());
	}

	for (int32 PayloadIndex = 0; PayloadIndex < PendingPayloads.Num(); ++PayloadIndex)
	{
		const FString& Payload = PendingPayloads[PayloadIndex];
		const FString& PayloadType = PendingTypes.IsValidIndex(PayloadIndex) && !PendingTypes[PayloadIndex].IsEmpty()
			? PendingTypes[PayloadIndex]
			: TEXT("audit");

		const FTCHARToUTF8 Utf8Payload(*Payload);
		const uint8* Data = reinterpret_cast<const uint8*>(Utf8Payload.Get());
		uint8* MutableData = const_cast<uint8*>(Data);
		const int32 DataLen = Utf8Payload.Length();

		for (int32 ClientIndex = EventWsClients.Num() - 1; ClientIndex >= 0; --ClientIndex)
		{
			if (!EventWsClients[ClientIndex].Socket)
			{
				EventWsClients.RemoveAtSwap(ClientIndex);
				continue;
			}
			if (!EventWsClients[ClientIndex].bSubscriptionConfirmed)
			{
				continue;
			}
			if (EventWsClients[ClientIndex].bEventTypeFilterEnabled
				&& !EventWsClients[ClientIndex].EventTypes.Contains(PayloadType))
			{
				continue;
			}

			EventWsClients[ClientIndex].Socket->Send(MutableData, DataLen, false);
		}
	}
#endif
}

bool FNovaBridgeRuntimeModule::HandleEvents(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TArray<FString> FilterTypes = ParseEventTypeFilter(Request);
	int32 PendingEvents = 0;
	int32 FilteredPendingEvents = 0;
	TArray<FString> PendingTypesSnapshot;
	{
		FScopeLock Lock(&RuntimeEventQueueMutex);
		PendingEvents = RuntimePendingEventPayloads.Num();
		PendingTypesSnapshot = RuntimePendingEventTypes;
	}

	TMap<FString, int32> PendingByType;
	for (const FString& PendingType : PendingTypesSnapshot)
	{
		PendingByType.FindOrAdd(PendingType)++;
	}

	int32 ClientsWithFilters = 0;
	int32 PendingSubscriptionClients = 0;
	for (const FWsClient& Client : EventWsClients)
	{
		if (!Client.Socket)
		{
			continue;
		}
		if (!Client.bSubscriptionConfirmed)
		{
			PendingSubscriptionClients++;
			continue;
		}
		if (Client.bEventTypeFilterEnabled)
		{
			ClientsWithFilters++;
		}
	}

	if (FilterTypes.Num() == 0)
	{
		FilteredPendingEvents = PendingEvents;
	}
	else
	{
		for (const FString& PendingType : PendingTypesSnapshot)
		{
			if (FilterTypes.Contains(PendingType))
			{
				FilteredPendingEvents++;
			}
		}
	}

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("route"), TEXT("/nova/events"));
	Result->SetStringField(TEXT("transport"), TEXT("websocket"));
	Result->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), EventWsPort));
	Result->SetNumberField(TEXT("ws_port"), EventWsPort);
	Result->SetNumberField(TEXT("clients"), EventWsClients.Num());
	Result->SetNumberField(TEXT("clients_with_filters"), ClientsWithFilters);
	Result->SetNumberField(TEXT("clients_pending_subscription"), PendingSubscriptionClients);
	Result->SetNumberField(TEXT("pending_events"), PendingEvents);
	Result->SetNumberField(TEXT("filtered_pending_events"), FilteredPendingEvents);
	Result->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedRuntimeEventTypes()));
	Result->SetStringField(TEXT("subscription_action"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
	if (FilterTypes.Num() > 0)
	{
		Result->SetArrayField(TEXT("filter_types"), MakeJsonStringArray(FilterTypes));
	}

	const TSharedPtr<FJsonObject> PendingByTypeObj = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : PendingByType)
	{
		PendingByTypeObj->SetNumberField(Pair.Key, Pair.Value);
	}
	Result->SetObjectField(TEXT("pending_by_type"), PendingByTypeObj);
#if NOVABRIDGE_WITH_WEBSOCKET_NETWORKING
	Result->SetBoolField(TEXT("websocket_available"), true);
#else
	Result->SetBoolField(TEXT("websocket_available"), false);
#endif

	SendJsonResponse(OnComplete, Result);
	return true;
}
