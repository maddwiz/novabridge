#include "NovaBridgeRuntimeModule.h"

#include "NovaBridgeHttpUtils.h"

#include "Dom/JsonObject.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
static const TCHAR* RuntimeModeName = TEXT("runtime");
using NovaBridgeCore::NormalizeEventType;
} // namespace

void FNovaBridgeRuntimeModule::QueueRuntimeEvent(const TSharedPtr<FJsonObject>& EventObj)
{
	if (!EventObj.IsValid())
	{
		return;
	}
	if (!EventObj->HasTypedField<EJson::String>(TEXT("type")))
	{
		EventObj->SetStringField(TEXT("type"), TEXT("audit"));
	}
	if (!EventObj->HasTypedField<EJson::String>(TEXT("mode")))
	{
		EventObj->SetStringField(TEXT("mode"), RuntimeModeName);
	}
	if (!EventObj->HasTypedField<EJson::String>(TEXT("timestamp_utc")))
	{
		EventObj->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	}

	const FString EventType = NormalizeEventType(EventObj->GetStringField(TEXT("type")));
	FString SerializedEvent;
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SerializedEvent);
		FJsonSerializer::Serialize(EventObj.ToSharedRef(), Writer);
	}

	FScopeLock EventLock(&RuntimeEventQueueMutex);
	RuntimePendingEventPayloads.Add(MoveTemp(SerializedEvent));
	RuntimePendingEventTypes.Add(EventType);
	if (RuntimePendingEventPayloads.Num() > RuntimePendingEventsLimit)
	{
		const int32 RemoveCount = RuntimePendingEventPayloads.Num() - RuntimePendingEventsLimit;
		RuntimePendingEventPayloads.RemoveAt(0, RemoveCount, EAllowShrinking::No);
		RuntimePendingEventTypes.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
}

void FNovaBridgeRuntimeModule::PushAuditEntry(const FString& Route, const FString& Action, const FString& Status, const FString& Message)
{
	FRuntimeAuditEntry Entry;
	Entry.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Entry.Route = Route;
	Entry.Action = Action;
	Entry.Status = Status;
	Entry.Message = Message;

	FScopeLock Lock(&RuntimeAuditMutex);
	RuntimeAuditTrail.Add(Entry);
	if (RuntimeAuditTrail.Num() > RuntimeAuditLimit)
	{
		RuntimeAuditTrail.RemoveAt(0, RuntimeAuditTrail.Num() - RuntimeAuditLimit, EAllowShrinking::No);
	}

	const TSharedPtr<FJsonObject> EventObj = MakeShared<FJsonObject>();
	EventObj->SetStringField(TEXT("type"), TEXT("audit"));
	EventObj->SetStringField(TEXT("mode"), RuntimeModeName);
	EventObj->SetStringField(TEXT("timestamp_utc"), Entry.TimestampUtc);
	EventObj->SetStringField(TEXT("route"), Entry.Route);
	EventObj->SetStringField(TEXT("action"), Entry.Action);
	EventObj->SetStringField(TEXT("status"), Entry.Status);
	EventObj->SetStringField(TEXT("message"), Entry.Message);
	QueueRuntimeEvent(EventObj);
}

void FNovaBridgeRuntimeModule::PushRuntimeUndoEntry(const FString& Action, const FString& ActorName, const FString& ActorLabel)
{
	FRuntimeUndoEntry Entry;
	Entry.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Entry.Action = Action;
	Entry.ActorName = ActorName;
	Entry.ActorLabel = ActorLabel;

	FScopeLock Lock(&RuntimeUndoMutex);
	RuntimeUndoStack.Add(Entry);
	if (RuntimeUndoStack.Num() > RuntimeUndoLimit)
	{
		RuntimeUndoStack.RemoveAt(0, RuntimeUndoStack.Num() - RuntimeUndoLimit, EAllowShrinking::No);
	}
}

bool FNovaBridgeRuntimeModule::PopRuntimeUndoEntry(FRuntimeUndoEntry& OutEntry)
{
	FScopeLock Lock(&RuntimeUndoMutex);
	if (RuntimeUndoStack.Num() == 0)
	{
		return false;
	}

	OutEntry = RuntimeUndoStack.Last();
	RuntimeUndoStack.Pop(EAllowShrinking::No);
	return true;
}
