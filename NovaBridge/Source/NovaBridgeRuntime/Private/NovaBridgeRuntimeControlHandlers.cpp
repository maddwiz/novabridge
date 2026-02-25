#include "NovaBridgeRuntimeModule.h"

#include "NovaBridgeCapabilityRegistry.h"
#include "NovaBridgeCoreTypes.h"
#include "NovaBridgeHttpUtils.h"
#include "NovaBridgePlanSchema.h"
#include "NovaBridgePolicy.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"

namespace
{
static const TCHAR* RuntimeModeName = TEXT("runtime");
using NovaBridgeCore::MakeJsonStringArray;

const TArray<FString>& RuntimeAllowedClasses()
{
	return NovaBridgeCore::RuntimeAllowedSpawnClasses();
}

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

TSharedPtr<FJsonObject> BuildRuntimeSpawnBoundsJson()
{
	const FVector& MinSpawnBounds = NovaBridgeCore::MinSpawnBounds();
	const FVector& MaxSpawnBounds = NovaBridgeCore::MaxSpawnBounds();

	const TSharedPtr<FJsonObject> SpawnBounds = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject> MinBounds = MakeShared<FJsonObject>();
	MinBounds->SetNumberField(TEXT("x"), MinSpawnBounds.X);
	MinBounds->SetNumberField(TEXT("y"), MinSpawnBounds.Y);
	MinBounds->SetNumberField(TEXT("z"), MinSpawnBounds.Z);
	const TSharedPtr<FJsonObject> MaxBounds = MakeShared<FJsonObject>();
	MaxBounds->SetNumberField(TEXT("x"), MaxSpawnBounds.X);
	MaxBounds->SetNumberField(TEXT("y"), MaxSpawnBounds.Y);
	MaxBounds->SetNumberField(TEXT("z"), MaxSpawnBounds.Z);
	SpawnBounds->SetObjectField(TEXT("min"), MinBounds);
	SpawnBounds->SetObjectField(TEXT("max"), MaxBounds);
	return SpawnBounds;
}
} // namespace

void FNovaBridgeRuntimeModule::RegisterRuntimeCapabilities()
{
	using namespace NovaBridgeCore;

	FCapabilityRegistry& Registry = FCapabilityRegistry::Get();
	Registry.Reset();

	auto RegisterCapability = [&Registry](const FString& Action, const TSharedPtr<FJsonObject>& Data)
	{
		FCapabilityRecord Capability;
		Capability.Action = Action;
		Capability.Data = Data.IsValid() ? Data : MakeShared<FJsonObject>();
		Registry.RegisterCapability(Capability);
	};

	TArray<TSharedPtr<FJsonValue>> AllowedClassValues;
	for (const FString& AllowedClass : RuntimeAllowedClasses())
	{
		AllowedClassValues.Add(MakeShared<FJsonValueString>(AllowedClass));
	}

	const TSharedPtr<FJsonObject> SpawnData = MakeShared<FJsonObject>();
	SpawnData->SetArrayField(TEXT("allowedClasses"), AllowedClassValues);
	SpawnData->SetObjectField(TEXT("bounds"), BuildRuntimeSpawnBoundsJson());
	SpawnData->SetNumberField(TEXT("max_spawn_per_plan"), MaxSpawnPerPlan);
	RegisterCapability(TEXT("spawn"), SpawnData);

	RegisterCapability(TEXT("delete"), MakeShared<FJsonObject>());
	RegisterCapability(TEXT("set"), MakeShared<FJsonObject>());

	const TSharedPtr<FJsonObject> ExecutePlanData = MakeShared<FJsonObject>();
	ExecutePlanData->SetNumberField(TEXT("max_steps"), MaxPlanSteps);
	ExecutePlanData->SetNumberField(TEXT("max_requests_per_minute"), MaxExecutePlanPerMinute);
	ExecutePlanData->SetArrayField(
		TEXT("actions"),
		MakeJsonStringArray(NovaBridgeCore::GetSupportedPlanActionsRef(NovaBridgeCore::ENovaBridgePlanMode::Runtime)));
	RegisterCapability(TEXT("executePlan"), ExecutePlanData);

	const TSharedPtr<FJsonObject> UndoData = MakeShared<FJsonObject>();
	UndoData->SetStringField(TEXT("supported"), TEXT("spawn"));
	RegisterCapability(TEXT("undo"), UndoData);

	const TSharedPtr<FJsonObject> AuditData = MakeShared<FJsonObject>();
	AuditData->SetStringField(TEXT("endpoint"), TEXT("/nova/audit"));
	RegisterCapability(TEXT("audit"), AuditData);

	const TSharedPtr<FJsonObject> EventsData = MakeShared<FJsonObject>();
	EventsData->SetStringField(TEXT("endpoint"), TEXT("/nova/events"));
	EventsData->SetStringField(TEXT("ws_url"), FString::Printf(TEXT("ws://localhost:%d"), EventWsPort));
	EventsData->SetArrayField(TEXT("supported_types"), MakeJsonStringArray(SupportedRuntimeEventTypes()));
	EventsData->SetStringField(TEXT("filter_query_param"), TEXT("types"));
	EventsData->SetStringField(TEXT("subscription_action"), TEXT("{\"action\":\"subscribe\",\"types\":[\"spawn\",\"error\"]}"));
	RegisterCapability(TEXT("events"), EventsData);
}

TSharedPtr<FJsonObject> FNovaBridgeRuntimeModule::BuildRuntimePermissionsSnapshot() const
{
	const TSharedPtr<FJsonObject> Permissions = MakeShared<FJsonObject>();
	Permissions->SetStringField(TEXT("mode"), RuntimeModeName);
	Permissions->SetBoolField(TEXT("localhost_only"), true);
	Permissions->SetBoolField(TEXT("pairing_required"), true);
	Permissions->SetBoolField(TEXT("token_required"), true);

	TArray<TSharedPtr<FJsonValue>> AllowedClassValues;
	for (const FString& AllowedClass : RuntimeAllowedClasses())
	{
		AllowedClassValues.Add(MakeShared<FJsonValueString>(AllowedClass));
	}

	const TSharedPtr<FJsonObject> SpawnPolicy = MakeShared<FJsonObject>();
	SpawnPolicy->SetBoolField(TEXT("allowed"), true);
	SpawnPolicy->SetArrayField(TEXT("allowedClasses"), AllowedClassValues);
	SpawnPolicy->SetObjectField(TEXT("bounds"), BuildRuntimeSpawnBoundsJson());
	SpawnPolicy->SetNumberField(TEXT("max_spawn_per_plan"), MaxSpawnPerPlan);
	Permissions->SetObjectField(TEXT("spawn"), SpawnPolicy);

	const TSharedPtr<FJsonObject> ExecutePlanPolicy = MakeShared<FJsonObject>();
	ExecutePlanPolicy->SetBoolField(TEXT("allowed"), true);
	ExecutePlanPolicy->SetArrayField(
		TEXT("allowed_actions"),
		MakeJsonStringArray(NovaBridgeCore::GetSupportedPlanActionsRef(NovaBridgeCore::ENovaBridgePlanMode::Runtime)));
	ExecutePlanPolicy->SetNumberField(TEXT("max_steps"), MaxPlanSteps);
	ExecutePlanPolicy->SetNumberField(TEXT("max_requests_per_minute"), MaxExecutePlanPerMinute);
	Permissions->SetObjectField(TEXT("executePlan"), ExecutePlanPolicy);

	const TSharedPtr<FJsonObject> UndoPolicy = MakeShared<FJsonObject>();
	UndoPolicy->SetBoolField(TEXT("allowed"), true);
	UndoPolicy->SetStringField(TEXT("supported"), TEXT("spawn"));
	Permissions->SetObjectField(TEXT("undo"), UndoPolicy);

	const TSharedPtr<FJsonObject> EventsPolicy = MakeShared<FJsonObject>();
	EventsPolicy->SetBoolField(TEXT("allowed"), true);
	EventsPolicy->SetBoolField(TEXT("subscription_ack_required"), true);
	Permissions->SetObjectField(TEXT("events"), EventsPolicy);
	return Permissions;
}

bool FNovaBridgeRuntimeModule::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	const int32 AuditCount = [&]()
	{
		FScopeLock Lock(&RuntimeAuditMutex);
		return RuntimeAuditTrail.Num();
	}();

	const TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("status"), TEXT("ok"));
	JsonObj->SetStringField(TEXT("version"), NovaBridgeCore::PluginVersion);
	JsonObj->SetStringField(TEXT("mode"), RuntimeModeName);
	JsonObj->SetStringField(TEXT("engine"), TEXT("UnrealEngine"));
	JsonObj->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	JsonObj->SetNumberField(TEXT("port"), HttpPort);
	JsonObj->SetNumberField(TEXT("routes"), ApiRouteCount);
	JsonObj->SetNumberField(TEXT("events_ws_port"), EventWsPort);
	JsonObj->SetBoolField(TEXT("token_required"), bRequireRuntimeToken);
	JsonObj->SetBoolField(TEXT("localhost_only"), true);
	JsonObj->SetNumberField(TEXT("audit_entries"), AuditCount);
	JsonObj->SetStringField(TEXT("token_expires_utc"), RuntimeTokenExpiryUtc.ToIso8601());
	SendJsonResponse(OnComplete, JsonObj);
	return true;
}

bool FNovaBridgeRuntimeModule::HandleCapabilities(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	(void)Request;
	TArray<TSharedPtr<FJsonValue>> Capabilities;
	TArray<NovaBridgeCore::FCapabilityRecord> Snapshot = NovaBridgeCore::FCapabilityRegistry::Get().Snapshot();
	if (Snapshot.Num() == 0)
	{
		RegisterRuntimeCapabilities();
		Snapshot = NovaBridgeCore::FCapabilityRegistry::Get().Snapshot();
	}
	Capabilities.Reserve(Snapshot.Num());
	for (const NovaBridgeCore::FCapabilityRecord& Capability : Snapshot)
	{
		Capabilities.Add(MakeShared<FJsonValueObject>(NovaBridgeCore::CapabilityToJson(Capability)));
	}

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("mode"), RuntimeModeName);
	Result->SetStringField(TEXT("version"), NovaBridgeCore::PluginVersion);
	Result->SetObjectField(TEXT("permissions"), BuildRuntimePermissionsSnapshot());
	Result->SetArrayField(TEXT("capabilities"), Capabilities);
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeRuntimeModule::HandleAuditTrail(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	int32 Limit = 50;
	if (Request.QueryParams.Contains(TEXT("limit")))
	{
		Limit = FMath::Clamp(FCString::Atoi(*Request.QueryParams[TEXT("limit")]), 1, 500);
	}

	TArray<FRuntimeAuditEntry> Snapshot;
	{
		FScopeLock Lock(&RuntimeAuditMutex);
		Snapshot = RuntimeAuditTrail;
	}

	const int32 StartIndex = FMath::Max(0, Snapshot.Num() - Limit);
	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Reserve(Snapshot.Num() - StartIndex);
	for (int32 Index = StartIndex; Index < Snapshot.Num(); ++Index)
	{
		const FRuntimeAuditEntry& Entry = Snapshot[Index];
		const TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
		EntryObj->SetStringField(TEXT("timestamp_utc"), Entry.TimestampUtc);
		EntryObj->SetStringField(TEXT("route"), Entry.Route);
		EntryObj->SetStringField(TEXT("action"), Entry.Action);
		EntryObj->SetStringField(TEXT("status"), Entry.Status);
		EntryObj->SetStringField(TEXT("message"), Entry.Message);
		Entries.Add(MakeShared<FJsonValueObject>(EntryObj));
	}

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetNumberField(TEXT("count"), Entries.Num());
	Result->SetNumberField(TEXT("total"), Snapshot.Num());
	Result->SetArrayField(TEXT("entries"), Entries);
	SendJsonResponse(OnComplete, Result);
	return true;
}

bool FNovaBridgeRuntimeModule::HandlePair(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const TSharedPtr<FJsonObject> Body = ParseRequestBody(Request);
	if (!Body || !Body->HasTypedField<EJson::String>(TEXT("code")))
	{
		PushAuditEntry(TEXT("/nova/runtime/pair"), TEXT("runtime.pair"), TEXT("error"), TEXT("Missing pairing code"));
		SendErrorResponse(OnComplete, TEXT("Missing pairing code. Provide {\"code\":\"<6-digit>\"}"), 400);
		return true;
	}

	const FString PresentedCode = Body->GetStringField(TEXT("code"));
	const FDateTime NowUtc = FDateTime::UtcNow();
	if (NowUtc > RuntimePairingExpiryUtc)
	{
		const int32 NewCode = FMath::RandRange(100000, 999999);
		RuntimePairingCode = FString::Printf(TEXT("%06d"), NewCode);
		RuntimePairingExpiryUtc = NowUtc + FTimespan::FromMinutes(15.0);
		UE_LOG(LogNovaBridgeRuntime, Warning, TEXT("Runtime pairing code expired; new code generated: %s"), *RuntimePairingCode);
		PushAuditEntry(TEXT("/nova/runtime/pair"), TEXT("runtime.pair"), TEXT("error"), TEXT("Pairing code expired"));
		SendErrorResponse(OnComplete, TEXT("Pairing code expired. Check runtime logs for the refreshed code."), 410);
		return true;
	}

	if (PresentedCode != RuntimePairingCode)
	{
		PushAuditEntry(TEXT("/nova/runtime/pair"), TEXT("runtime.pair"), TEXT("denied"), TEXT("Invalid pairing code"));
		SendErrorResponse(OnComplete, TEXT("Invalid pairing code."), 403);
		return true;
	}

	RuntimeToken = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	RuntimeTokenIssuedUtc = NowUtc;
	RuntimeTokenExpiryUtc = NowUtc + FTimespan::FromHours(1.0);
	const int32 RefreshedCode = FMath::RandRange(100000, 999999);
	RuntimePairingCode = FString::Printf(TEXT("%06d"), RefreshedCode);
	RuntimePairingExpiryUtc = NowUtc + FTimespan::FromMinutes(15.0);
	UE_LOG(LogNovaBridgeRuntime, Log, TEXT("Runtime pair success; pairing code rotated to %s"), *RuntimePairingCode);

	const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("ok"));
	Result->SetStringField(TEXT("mode"), RuntimeModeName);
	Result->SetStringField(TEXT("token"), RuntimeToken);
	Result->SetStringField(TEXT("token_expires_utc"), RuntimeTokenExpiryUtc.ToIso8601());
	SendJsonResponse(OnComplete, Result);
	PushAuditEntry(TEXT("/nova/runtime/pair"), TEXT("runtime.pair"), TEXT("success"), TEXT("Runtime pair succeeded and token rotated"));
	return true;
}
