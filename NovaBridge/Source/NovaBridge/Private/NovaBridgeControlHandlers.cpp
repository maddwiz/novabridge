#include "NovaBridgeModule.h"
#include "NovaBridgeEditorInternals.h"

#include "NovaBridgeCapabilityRegistry.h"
#include "NovaBridgeCoreTypes.h"
#include "NovaBridgeHttpUtils.h"
#include "NovaBridgePolicy.h"
#include "NovaBridgePlanSchema.h"
#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Subsystems/EditorActorSubsystem.h"

using NovaBridgeCore::MakeJsonStringArray;
using NovaBridgeCore::ParseEventTypeFilter;

bool FNovaBridgeModule::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	JsonObj->SetStringField(TEXT("version"), NovaBridgeCore::PluginVersion);
	JsonObj->SetStringField(TEXT("engine"), TEXT("UnrealEngine"));
	JsonObj->SetStringField(TEXT("mode"), TEXT("editor"));
	JsonObj->SetNumberField(TEXT("port"), HttpPort);
	JsonObj->SetNumberField(TEXT("stream_ws_port"), WsPort);
	JsonObj->SetNumberField(TEXT("events_ws_port"), EventWsPort);
	JsonObj->SetNumberField(TEXT("routes"), ApiRouteCount);
	JsonObj->SetBoolField(TEXT("api_key_required"), !RequiredApiKey.IsEmpty());
	JsonObj->SetStringField(TEXT("default_role"), GetNovaBridgeDefaultRole());
	SendJsonResponse(OnComplete, JsonObj);
	return true;
}

bool FNovaBridgeModule::HandleProjectInfo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	JsonObj->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	JsonObj->SetStringField(TEXT("project_file"), FPaths::GetProjectFilePath());
	JsonObj->SetStringField(TEXT("project_dir"), FPaths::ProjectDir());
	JsonObj->SetStringField(TEXT("content_dir"), FPaths::ProjectContentDir());
	SendJsonResponse(OnComplete, JsonObj);
	return true;
}

static TArray<FString> BuildCapabilityRoles(const bool bAdmin, const bool bAutomation, const bool bReadOnly)
{
	TArray<FString> Roles;
	if (bAdmin)
	{
		Roles.Add(TEXT("admin"));
	}
	if (bAutomation)
	{
		Roles.Add(TEXT("automation"));
	}
	if (bReadOnly)
	{
		Roles.Add(TEXT("read_only"));
	}
	return Roles;
}

static TSharedPtr<FJsonObject> BuildSpawnBoundsJson()
{
	const FVector& MinSpawnBounds = NovaBridgeCore::MinSpawnBounds();
	const FVector& MaxSpawnBounds = NovaBridgeCore::MaxSpawnBounds();
	TSharedPtr<FJsonObject> SpawnBounds = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> MinBounds = MakeShared<FJsonObject>();
	MinBounds->SetNumberField(TEXT("x"), MinSpawnBounds.X);
	MinBounds->SetNumberField(TEXT("y"), MinSpawnBounds.Y);
	MinBounds->SetNumberField(TEXT("z"), MinSpawnBounds.Z);
	TSharedPtr<FJsonObject> MaxBounds = MakeShared<FJsonObject>();
	MaxBounds->SetNumberField(TEXT("x"), MaxSpawnBounds.X);
	MaxBounds->SetNumberField(TEXT("y"), MaxSpawnBounds.Y);
	MaxBounds->SetNumberField(TEXT("z"), MaxSpawnBounds.Z);
	SpawnBounds->SetObjectField(TEXT("min"), MinBounds);
	SpawnBounds->SetObjectField(TEXT("max"), MaxBounds);
	return SpawnBounds;
}

static TSharedPtr<FJsonObject> BuildEditorPermissionsSnapshot(const FString& Role)
{
	TSharedPtr<FJsonObject> Permissions = MakeShared<FJsonObject>();
	Permissions->SetStringField(TEXT("mode"), TEXT("editor"));
	Permissions->SetStringField(TEXT("role"), Role);

	const bool bSpawnRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/scene/spawn"), EHttpServerRequestVerbs::VERB_POST);
	const bool bExecutePlanRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/executePlan"), EHttpServerRequestVerbs::VERB_POST);
	const bool bUndoRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/undo"), EHttpServerRequestVerbs::VERB_POST);
	const bool bEventsRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/events"), EHttpServerRequestVerbs::VERB_GET);
	const bool bSceneDeleteRouteAllowed = IsRouteAllowedForRole(Role, TEXT("/nova/scene/delete"), EHttpServerRequestVerbs::VERB_POST);

	TArray<TSharedPtr<FJsonValue>> AllowedClassValues;
	for (const FString& AllowedClass : NovaBridgeCore::EditorAllowedSpawnClasses())
	{
		AllowedClassValues.Add(MakeShared<FJsonValueString>(AllowedClass));
	}

	TSharedPtr<FJsonObject> SpawnPolicy = MakeShared<FJsonObject>();
	SpawnPolicy->SetBoolField(TEXT("allowed"), IsPlanActionAllowedForRole(Role, TEXT("spawn")) && bSpawnRouteAllowed);
	SpawnPolicy->SetBoolField(TEXT("classes_unrestricted"), Role == TEXT("admin"));
	SpawnPolicy->SetArrayField(TEXT("allowedClasses"), AllowedClassValues);
	SpawnPolicy->SetObjectField(TEXT("bounds"), BuildSpawnBoundsJson());
	SpawnPolicy->SetNumberField(TEXT("max_spawn_per_plan"), GetPlanSpawnLimit(Role));
	SpawnPolicy->SetNumberField(TEXT("max_requests_per_minute"), bSpawnRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/scene/spawn")) : 0);
	Permissions->SetObjectField(TEXT("spawn"), SpawnPolicy);

	TArray<FString> AllowedPlanActions;
	for (const FString& Action : NovaBridgeCore::GetSupportedPlanActionsRef(NovaBridgeCore::ENovaBridgePlanMode::Editor))
	{
		if (IsPlanActionAllowedForRole(Role, Action))
		{
			AllowedPlanActions.Add(Action);
		}
	}

	TSharedPtr<FJsonObject> ExecutePlanPolicy = MakeShared<FJsonObject>();
	ExecutePlanPolicy->SetBoolField(TEXT("allowed"), bExecutePlanRouteAllowed);
	ExecutePlanPolicy->SetArrayField(TEXT("allowed_actions"), MakeJsonStringArray(AllowedPlanActions));
	ExecutePlanPolicy->SetNumberField(TEXT("max_steps"), GetNovaBridgeEditorMaxPlanSteps());
	ExecutePlanPolicy->SetNumberField(TEXT("max_requests_per_minute"), bExecutePlanRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/executePlan")) : 0);
	Permissions->SetObjectField(TEXT("executePlan"), ExecutePlanPolicy);

	TSharedPtr<FJsonObject> UndoPolicy = MakeShared<FJsonObject>();
	UndoPolicy->SetBoolField(TEXT("allowed"), bUndoRouteAllowed);
	UndoPolicy->SetStringField(TEXT("supported"), TEXT("spawn"));
	Permissions->SetObjectField(TEXT("undo"), UndoPolicy);

	TSharedPtr<FJsonObject> EventsPolicy = MakeShared<FJsonObject>();
	EventsPolicy->SetBoolField(TEXT("allowed"), bEventsRouteAllowed);
	EventsPolicy->SetBoolField(TEXT("subscription_ack_required"), true);
	Permissions->SetObjectField(TEXT("events"), EventsPolicy);

	TSharedPtr<FJsonObject> RateLimits = MakeShared<FJsonObject>();
	RateLimits->SetNumberField(TEXT("executePlan"), bExecutePlanRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/executePlan")) : 0);
	RateLimits->SetNumberField(TEXT("scene_spawn"), bSpawnRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/scene/spawn")) : 0);
	RateLimits->SetNumberField(TEXT("scene_delete"), bSceneDeleteRouteAllowed ? GetRouteRateLimitPerMinute(Role, TEXT("/nova/scene/delete")) : 0);
	Permissions->SetObjectField(TEXT("route_rate_limits_per_minute"), RateLimits);

	return Permissions;
}

void RegisterEditorCapabilities(uint32 InEventWsPort)
{
	using namespace NovaBridgeCore;

	FCapabilityRegistry& Registry = FCapabilityRegistry::Get();
	Registry.Reset();

	auto RegisterCapability = [&Registry](const FString& Action, const TArray<FString>& Roles, const TSharedPtr<FJsonObject>& Data)
	{
		FCapabilityRecord Capability;
		Capability.Action = Action;
		Capability.Roles = Roles;
		Capability.Data = Data.IsValid() ? Data : MakeShared<FJsonObject>();
		Registry.RegisterCapability(Capability);
	};

	TArray<TSharedPtr<FJsonValue>> AllowedClassValues;
	for (const FString& AllowedClass : NovaBridgeCore::EditorAllowedSpawnClasses())
	{
		AllowedClassValues.Add(MakeShared<FJsonValueString>(AllowedClass));
	}

	TSharedPtr<FJsonObject> SpawnData = MakeShared<FJsonObject>();
	SpawnData->SetArrayField(TEXT("allowedClasses"), AllowedClassValues);
	SpawnData->SetObjectField(TEXT("bounds"), BuildSpawnBoundsJson());
	SpawnData->SetNumberField(TEXT("max_spawn_per_plan_admin"), GetPlanSpawnLimit(TEXT("admin")));
	SpawnData->SetNumberField(TEXT("max_spawn_per_plan_automation"), GetPlanSpawnLimit(TEXT("automation")));
	RegisterCapability(TEXT("spawn"), BuildCapabilityRoles(true, true, false), SpawnData);

	RegisterCapability(TEXT("delete"), BuildCapabilityRoles(true, true, false), MakeShared<FJsonObject>());
	RegisterCapability(TEXT("set"), BuildCapabilityRoles(true, true, false), MakeShared<FJsonObject>());
	RegisterCapability(TEXT("screenshot"), BuildCapabilityRoles(true, true, true), MakeShared<FJsonObject>());

	TSharedPtr<FJsonObject> EventsData = MakeShared<FJsonObject>();
	EventsData->SetStringField(TEXT("endpoint"), TEXT("/nova/events"));
	EventsData->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), InEventWsPort));
	EventsData->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedEventTypes()));
	EventsData->SetStringField(TEXT("filter_query_param"), TEXT("types"));
	EventsData->SetStringField(TEXT("subscription_action"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
	RegisterCapability(TEXT("events"), BuildCapabilityRoles(true, true, true), EventsData);

	TSharedPtr<FJsonObject> ExecutePlanData = MakeShared<FJsonObject>();
	ExecutePlanData->SetNumberField(TEXT("max_steps"), GetNovaBridgeEditorMaxPlanSteps());
	ExecutePlanData->SetArrayField(
		TEXT("actions"),
		MakeJsonStringArray(NovaBridgeCore::GetSupportedPlanActionsRef(NovaBridgeCore::ENovaBridgePlanMode::Editor)));
	RegisterCapability(TEXT("executePlan"), BuildCapabilityRoles(true, true, false), ExecutePlanData);

	TSharedPtr<FJsonObject> UndoData = MakeShared<FJsonObject>();
	UndoData->SetStringField(TEXT("supported"), TEXT("spawn"));
	RegisterCapability(TEXT("undo"), BuildCapabilityRoles(true, true, false), UndoData);
}

bool FNovaBridgeModule::HandleCapabilities(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString Role = ResolveRoleFromRequest(Request);
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("mode"), TEXT("editor"));
	Result->SetStringField(TEXT("version"), NovaBridgeCore::PluginVersion);
	Result->SetStringField(TEXT("default_role"), GetNovaBridgeDefaultRole());
	Result->SetStringField(TEXT("role"), Role);
	Result->SetObjectField(TEXT("permissions"), BuildEditorPermissionsSnapshot(Role));

	TArray<TSharedPtr<FJsonValue>> Capabilities;
	TArray<NovaBridgeCore::FCapabilityRecord> Snapshot = NovaBridgeCore::FCapabilityRegistry::Get().Snapshot();
	if (Snapshot.Num() == 0)
	{
		RegisterEditorCapabilities(EventWsPort);
		Snapshot = NovaBridgeCore::FCapabilityRegistry::Get().Snapshot();
	}
	Capabilities.Reserve(Snapshot.Num());
	for (const NovaBridgeCore::FCapabilityRecord& Capability : Snapshot)
	{
		if (!NovaBridgeCore::IsCapabilityAllowedForRole(Capability, Role))
		{
			continue;
		}
		Capabilities.Add(MakeShareable(new FJsonValueObject(NovaBridgeCore::CapabilityToJson(Capability))));
	}

	Result->SetArrayField(TEXT("capabilities"), Capabilities);
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleEvents(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TArray<FString> FilterTypes = ParseEventTypeFilter(Request);
	int32 PendingEvents = 0;
	int32 FilteredPendingEvents = 0;
	TArray<FString> PendingTypesSnapshot;
	GetPendingEventSnapshot(PendingEvents, PendingTypesSnapshot);

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

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
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
	Result->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedEventTypes()));
	Result->SetStringField(TEXT("subscription_action"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
	if (FilterTypes.Num() > 0)
	{
		Result->SetArrayField(TEXT("filter_types"), MakeJsonStringArray(FilterTypes));
	}

	TSharedPtr<FJsonObject> PendingByTypeObj = MakeShared<FJsonObject>();
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

bool FNovaBridgeModule::HandleAuditTrail(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	int32 Limit = 50;
	if (Request.QueryParams.Contains(TEXT("limit")))
	{
		Limit = FMath::Clamp(FCString::Atoi(*Request.QueryParams[TEXT("limit")]), 1, 500);
	}

	const TArray<FNovaBridgeAuditEntry> Snapshot = GetAuditTrailSnapshot();

	const int32 StartIndex = FMath::Max(0, Snapshot.Num() - Limit);
	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Reserve(Snapshot.Num() - StartIndex);
	for (int32 Index = StartIndex; Index < Snapshot.Num(); ++Index)
	{
		const FNovaBridgeAuditEntry& Entry = Snapshot[Index];
		TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject);
		EntryObj->SetStringField(TEXT("timestamp_utc"), Entry.TimestampUtc);
		EntryObj->SetStringField(TEXT("route"), Entry.Route);
		EntryObj->SetStringField(TEXT("action"), Entry.Action);
		EntryObj->SetStringField(TEXT("role"), Entry.Role);
		EntryObj->SetStringField(TEXT("status"), Entry.Status);
		EntryObj->SetStringField(TEXT("message"), Entry.Message);
		Entries.Add(MakeShareable(new FJsonValueObject(EntryObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetNumberField(TEXT("count"), Entries.Num());
	Result->SetNumberField(TEXT("total"), Snapshot.Num());
	Result->SetArrayField(TEXT("entries"), Entries);
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeModule::HandleUndo(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString Role = ResolveRoleFromRequest(Request);
	if (Role == TEXT("read_only"))
	{
		SendErrorResponse(OnComplete, TEXT("Role 'read_only' cannot execute undo"), 403);
		return true;
	}

	AsyncTask(ENamedThreads::GameThread, [this, OnComplete, Role]()
	{
		FNovaBridgeUndoEntry Entry;
		if (!PopUndoEntry(Entry))
		{
			SendErrorResponse(OnComplete, TEXT("Undo stack is empty"), 404);
			return;
		}

		if (Entry.Action == TEXT("spawn"))
		{
			AActor* Actor = FindActorByName(Entry.ActorName);
			bool bDeleted = false;
			if (Actor)
			{
				if (UEditorActorSubsystem* ActorSub = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr)
				{
					ActorSub->DestroyActor(Actor);
					bDeleted = true;
				}
				else
				{
					Actor->Destroy();
					bDeleted = true;
				}
			}

			TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
			Result->SetStringField(TEXT("status"), TEXT("ok"));
			Result->SetStringField(TEXT("undone_action"), Entry.Action);
			Result->SetStringField(TEXT("actor_name"), Entry.ActorName);
			Result->SetStringField(TEXT("actor_label"), Entry.ActorLabel);
			Result->SetBoolField(TEXT("deleted"), bDeleted);
			SendJsonResponse(OnComplete, Result);
			PushAuditEntry(TEXT("/nova/undo"), TEXT("undo"), Role, TEXT("success"),
				FString::Printf(TEXT("Undo spawn for actor '%s'"), *Entry.ActorName));
			return;
		}

		SendErrorResponse(OnComplete, FString::Printf(TEXT("Unsupported undo action: %s"), *Entry.Action), 400);
		PushAuditEntry(TEXT("/nova/undo"), TEXT("undo"), Role, TEXT("error"),
			FString::Printf(TEXT("Unsupported undo action: %s"), *Entry.Action));
	});
	return true;
}
