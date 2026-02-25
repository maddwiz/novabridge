#include "NovaBridgeEditorInternals.h"

#include "NovaBridgeCoreTypes.h"
#include "NovaBridgeHttpUtils.h"
#include "NovaBridgePolicy.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

using NovaBridgeCore::GetHeaderValueCaseInsensitive;
using NovaBridgeCore::NormalizeEventType;

namespace
{
struct FNovaBridgeRateBucket
{
	double WindowStartSec = 0.0;
	int32 Count = 0;
};

FCriticalSection NovaBridgeRateLimitMutex;
TMap<FString, FNovaBridgeRateBucket> NovaBridgeRateBuckets;
FCriticalSection NovaBridgeUndoMutex;
TArray<FNovaBridgeUndoEntry> NovaBridgeUndoStack;
FCriticalSection NovaBridgeAuditMutex;
TArray<FNovaBridgeAuditEntry> NovaBridgeAuditTrail;
FCriticalSection NovaBridgeEventQueueMutex;
TArray<FString> NovaBridgePendingEventPayloads;
TArray<FString> NovaBridgePendingEventTypes;
FString NovaBridgeDefaultRole;
const int32 NovaBridgeUndoLimit = 128;
const int32 NovaBridgeAuditLimit = 512;
const int32 NovaBridgePendingEventsLimit = 2048;
const int32 NovaBridgeEditorMaxPlanSteps = 128;

const TArray<FString>& NovaBridgeSpawnClassAllowList()
{
	return NovaBridgeCore::EditorAllowedSpawnClasses();
}

bool IsReadOnlyRoute(const FString& RoutePath)
{
	return RoutePath == TEXT("/nova/health")
		|| RoutePath == TEXT("/nova/project/info")
		|| RoutePath == TEXT("/nova/caps")
		|| RoutePath == TEXT("/nova/events")
		|| RoutePath == TEXT("/nova/audit")
		|| RoutePath == TEXT("/nova/scene/list")
		|| RoutePath == TEXT("/nova/scene/get")
		|| RoutePath == TEXT("/nova/asset/list")
		|| RoutePath == TEXT("/nova/asset/info")
		|| RoutePath == TEXT("/nova/mesh/get")
		|| RoutePath == TEXT("/nova/material/get")
		|| RoutePath == TEXT("/nova/viewport/screenshot")
		|| RoutePath == TEXT("/nova/viewport/camera/get")
		|| RoutePath == TEXT("/nova/stream/status")
		|| RoutePath == TEXT("/nova/pcg/list-graphs")
		|| RoutePath == TEXT("/nova/sequencer/info")
		|| RoutePath == TEXT("/nova/optimize/stats");
}
} // namespace

FString ResolveRoleFromRequest(const FHttpServerRequest& Request)
{
	FString CandidateRole = GetHeaderValueCaseInsensitive(Request, TEXT("X-NovaBridge-Role"));
	if (CandidateRole.IsEmpty() && Request.QueryParams.Contains(TEXT("role")))
	{
		CandidateRole = Request.QueryParams[TEXT("role")];
	}

	FString Role = NovaBridgeCore::NormalizeRoleName(CandidateRole);
	if (Role.IsEmpty())
	{
		Role = NovaBridgeDefaultRole;
	}
	if (Role.IsEmpty())
	{
		Role = TEXT("admin");
	}
	return Role;
}

void SetNovaBridgeDefaultRole(const FString& InRole)
{
	NovaBridgeDefaultRole = NovaBridgeCore::NormalizeRoleName(InRole);
}

const FString& GetNovaBridgeDefaultRole()
{
	return NovaBridgeDefaultRole;
}

int32 GetNovaBridgeEditorMaxPlanSteps()
{
	return NovaBridgeEditorMaxPlanSteps;
}

void ResetNovaBridgeEditorControlState()
{
	{
		FScopeLock RateLock(&NovaBridgeRateLimitMutex);
		NovaBridgeRateBuckets.Empty();
	}
	{
		FScopeLock UndoLock(&NovaBridgeUndoMutex);
		NovaBridgeUndoStack.Empty();
	}
	{
		FScopeLock AuditLock(&NovaBridgeAuditMutex);
		NovaBridgeAuditTrail.Empty();
	}
	{
		FScopeLock EventLock(&NovaBridgeEventQueueMutex);
		NovaBridgePendingEventPayloads.Empty();
		NovaBridgePendingEventTypes.Empty();
	}
}

bool IsRouteAllowedForRole(const FString& Role, const FString& RoutePath, EHttpServerRequestVerbs Verb)
{
	if (Role == TEXT("admin"))
	{
		return true;
	}

	if (Role == TEXT("automation"))
	{
		if (RoutePath == TEXT("/nova/exec/command") || RoutePath == TEXT("/nova/build/lighting"))
		{
			return false;
		}
		return true;
	}

	if (Role == TEXT("read_only"))
	{
		if (Verb == EHttpServerRequestVerbs::VERB_GET)
		{
			return IsReadOnlyRoute(RoutePath);
		}
		return false;
	}

	return false;
}

int32 GetRouteRateLimitPerMinute(const FString& Role, const FString& RoutePath)
{
	if (Role == TEXT("admin"))
	{
		if (RoutePath == TEXT("/nova/executePlan"))
		{
			return 30;
		}
		if (RoutePath == TEXT("/nova/scene/spawn"))
		{
			return 120;
		}
		return 240;
	}

	if (Role == TEXT("automation"))
	{
		if (RoutePath == TEXT("/nova/executePlan"))
		{
			return 20;
		}
		if (RoutePath == TEXT("/nova/scene/spawn"))
		{
			return 60;
		}
		return 120;
	}

	if (Role == TEXT("read_only"))
	{
		return 120;
	}

	return 0;
}

bool ConsumeRateLimit(const FString& BucketKey, const int32 LimitPerMinute, FString& OutError)
{
	if (LimitPerMinute <= 0)
	{
		OutError = TEXT("Rate limit denied for this role/action");
		return false;
	}

	const double NowSec = FPlatformTime::Seconds();
	FScopeLock Lock(&NovaBridgeRateLimitMutex);
	FNovaBridgeRateBucket& Bucket = NovaBridgeRateBuckets.FindOrAdd(BucketKey);
	if (Bucket.WindowStartSec <= 0.0 || (NowSec - Bucket.WindowStartSec) >= 60.0)
	{
		Bucket.WindowStartSec = NowSec;
		Bucket.Count = 0;
	}

	Bucket.Count++;
	if (Bucket.Count > LimitPerMinute)
	{
		OutError = FString::Printf(TEXT("Rate limit: max %d requests/minute for this role/action"), LimitPerMinute);
		return false;
	}
	return true;
}

void PushUndoEntry(const FNovaBridgeUndoEntry& Entry)
{
	FScopeLock Lock(&NovaBridgeUndoMutex);
	NovaBridgeUndoStack.Add(Entry);
	if (NovaBridgeUndoStack.Num() > NovaBridgeUndoLimit)
	{
		NovaBridgeUndoStack.RemoveAt(0, NovaBridgeUndoStack.Num() - NovaBridgeUndoLimit, EAllowShrinking::No);
	}
}

bool PopUndoEntry(FNovaBridgeUndoEntry& OutEntry)
{
	FScopeLock Lock(&NovaBridgeUndoMutex);
	if (NovaBridgeUndoStack.Num() == 0)
	{
		return false;
	}
	OutEntry = NovaBridgeUndoStack.Last();
	NovaBridgeUndoStack.Pop();
	return true;
}

const TArray<FString>& SupportedEventTypes()
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

void QueueEventObject(const TSharedPtr<FJsonObject>& EventObj)
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
		EventObj->SetStringField(TEXT("mode"), TEXT("editor"));
	}
	if (!EventObj->HasTypedField<EJson::String>(TEXT("timestamp_utc")))
	{
		EventObj->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	}

	const FString EventType = NormalizeEventType(EventObj->GetStringField(TEXT("type")));

	FString SerializedEvent;
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&SerializedEvent);
		FJsonSerializer::Serialize(EventObj.ToSharedRef(), Writer);
	}

	FScopeLock EventLock(&NovaBridgeEventQueueMutex);
	NovaBridgePendingEventPayloads.Add(MoveTemp(SerializedEvent));
	NovaBridgePendingEventTypes.Add(EventType);
	if (NovaBridgePendingEventPayloads.Num() > NovaBridgePendingEventsLimit)
	{
		const int32 RemoveCount = NovaBridgePendingEventPayloads.Num() - NovaBridgePendingEventsLimit;
		NovaBridgePendingEventPayloads.RemoveAt(0, RemoveCount, EAllowShrinking::No);
		NovaBridgePendingEventTypes.RemoveAt(0, RemoveCount, EAllowShrinking::No);
	}
}

void GetPendingEventSnapshot(int32& OutPendingEvents, TArray<FString>& OutPendingTypes)
{
	FScopeLock EventLock(&NovaBridgeEventQueueMutex);
	OutPendingEvents = NovaBridgePendingEventPayloads.Num();
	OutPendingTypes = NovaBridgePendingEventTypes;
}

void DrainPendingEventQueue(TArray<FString>& OutPendingPayloads, TArray<FString>& OutPendingTypes)
{
	FScopeLock EventLock(&NovaBridgeEventQueueMutex);
	OutPendingPayloads = MoveTemp(NovaBridgePendingEventPayloads);
	OutPendingTypes = MoveTemp(NovaBridgePendingEventTypes);
	NovaBridgePendingEventPayloads.Reset();
	NovaBridgePendingEventTypes.Reset();
}

void PushAuditEntry(const FString& Route, const FString& Action, const FString& Role, const FString& Status, const FString& Message)
{
	FNovaBridgeAuditEntry Entry;
	Entry.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Entry.Route = Route;
	Entry.Action = Action;
	Entry.Role = Role;
	Entry.Status = Status;
	Entry.Message = Message;

	FScopeLock Lock(&NovaBridgeAuditMutex);
	NovaBridgeAuditTrail.Add(Entry);
	if (NovaBridgeAuditTrail.Num() > NovaBridgeAuditLimit)
	{
		NovaBridgeAuditTrail.RemoveAt(0, NovaBridgeAuditTrail.Num() - NovaBridgeAuditLimit, EAllowShrinking::No);
	}

	TSharedPtr<FJsonObject> EventObj = MakeShareable(new FJsonObject);
	EventObj->SetStringField(TEXT("type"), TEXT("audit"));
	EventObj->SetStringField(TEXT("mode"), TEXT("editor"));
	EventObj->SetStringField(TEXT("timestamp_utc"), Entry.TimestampUtc);
	EventObj->SetStringField(TEXT("route"), Entry.Route);
	EventObj->SetStringField(TEXT("action"), Entry.Action);
	EventObj->SetStringField(TEXT("role"), Entry.Role);
	EventObj->SetStringField(TEXT("status"), Entry.Status);
	EventObj->SetStringField(TEXT("message"), Entry.Message);
	QueueEventObject(EventObj);
}

TArray<FNovaBridgeAuditEntry> GetAuditTrailSnapshot()
{
	FScopeLock Lock(&NovaBridgeAuditMutex);
	return NovaBridgeAuditTrail;
}

bool IsSpawnClassAllowedForRole(const FString& Role, const FString& ClassName)
{
	if (Role == TEXT("admin"))
	{
		return true;
	}
	return NovaBridgeCore::IsClassAllowed(NovaBridgeSpawnClassAllowList(), ClassName);
}

bool IsSpawnLocationInBounds(const FVector& Location)
{
	return NovaBridgeCore::IsSpawnLocationInBounds(Location);
}

int32 GetPlanSpawnLimit(const FString& Role)
{
	return NovaBridgeCore::GetEditorPlanSpawnLimit(Role);
}

bool IsPlanActionAllowedForRole(const FString& Role, const FString& Action)
{
	return NovaBridgeCore::IsEditorPlanActionAllowedForRole(Role, Action);
}
