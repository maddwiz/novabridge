#include "NovaBridgePlanEvents.h"

namespace NovaBridgeCore
{
namespace
{
static FString NormalizeStatus(const FString& RawStatus)
{
	FString Status = RawStatus;
	Status.TrimStartAndEndInline();
	Status.ToLowerInline();
	return Status;
}

static TSharedPtr<FJsonObject> BuildBaseEvent(
	const FString& Type,
	const FString& Mode,
	const FString& Action,
	const FString& Status,
	const FString& PlanId,
	const int32 StepIndex)
{
	TSharedPtr<FJsonObject> EventObj = MakeShared<FJsonObject>();
	EventObj->SetStringField(TEXT("type"), Type);
	EventObj->SetStringField(TEXT("mode"), Mode);
	EventObj->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	EventObj->SetStringField(TEXT("route"), TEXT("/nova/executePlan"));
	EventObj->SetStringField(TEXT("action"), Action.IsEmpty() ? TEXT("unknown") : Action);
	EventObj->SetStringField(TEXT("status"), Status.IsEmpty() ? TEXT("error") : Status);
	EventObj->SetStringField(TEXT("plan_id"), PlanId);
	if (StepIndex != INDEX_NONE)
	{
		EventObj->SetNumberField(TEXT("step"), StepIndex);
	}
	return EventObj;
}
} // namespace

TSharedPtr<FJsonObject> BuildPlanStepEvent(
	const FString& Mode,
	const FString& PlanId,
	const FString& Action,
	const TSharedPtr<FJsonObject>& StepResultObj)
{
	if (!StepResultObj.IsValid())
	{
		return nullptr;
	}

	const int32 StepIndex = StepResultObj->HasTypedField<EJson::Number>(TEXT("step"))
		? static_cast<int32>(StepResultObj->GetNumberField(TEXT("step")))
		: INDEX_NONE;
	const FString Status = StepResultObj->HasTypedField<EJson::String>(TEXT("status"))
		? NormalizeStatus(StepResultObj->GetStringField(TEXT("status")))
		: TEXT("error");

	TSharedPtr<FJsonObject> EventObj = BuildBaseEvent(
		Status.Equals(TEXT("error"), ESearchCase::IgnoreCase) ? TEXT("error") : TEXT("plan_step"),
		Mode,
		Action,
		Status,
		PlanId,
		StepIndex);

	if (StepResultObj->HasTypedField<EJson::String>(TEXT("message")))
	{
		EventObj->SetStringField(TEXT("message"), StepResultObj->GetStringField(TEXT("message")));
	}
	if (StepResultObj->HasTypedField<EJson::String>(TEXT("object_id")))
	{
		EventObj->SetStringField(TEXT("object_id"), StepResultObj->GetStringField(TEXT("object_id")));
	}
	return EventObj;
}

TSharedPtr<FJsonObject> BuildPlanCompleteEvent(
	const FString& Mode,
	const FString& PlanId,
	const int32 StepCount,
	const int32 SuccessCount,
	const int32 ErrorCount,
	const FString& Role)
{
	TSharedPtr<FJsonObject> EventObj = BuildBaseEvent(
		TEXT("plan_complete"),
		Mode,
		TEXT("executePlan.complete"),
		ErrorCount == 0 ? TEXT("success") : TEXT("partial"),
		PlanId,
		INDEX_NONE);
	if (!Role.IsEmpty())
	{
		EventObj->SetStringField(TEXT("role"), Role);
	}
	EventObj->SetNumberField(TEXT("step_count"), StepCount);
	EventObj->SetNumberField(TEXT("success_count"), SuccessCount);
	EventObj->SetNumberField(TEXT("error_count"), ErrorCount);
	return EventObj;
}

TSharedPtr<FJsonObject> BuildSpawnEvent(
	const FString& Mode,
	const FString& PlanId,
	const int32 StepIndex,
	const FString& Action,
	const FString& ObjectId,
	const FString& ClassName,
	const FString& ActorName,
	const FString& ActorLabel,
	const FString& RequestedName)
{
	TSharedPtr<FJsonObject> EventObj = BuildBaseEvent(
		TEXT("spawn"),
		Mode,
		Action,
		TEXT("success"),
		PlanId,
		StepIndex);
	if (!ObjectId.IsEmpty())
	{
		EventObj->SetStringField(TEXT("object_id"), ObjectId);
	}
	if (!ClassName.IsEmpty())
	{
		EventObj->SetStringField(TEXT("class"), ClassName);
	}
	if (!ActorName.IsEmpty())
	{
		EventObj->SetStringField(TEXT("actor_name"), ActorName);
	}
	if (!ActorLabel.IsEmpty())
	{
		EventObj->SetStringField(TEXT("actor_label"), ActorLabel);
	}
	if (!RequestedName.IsEmpty())
	{
		EventObj->SetStringField(TEXT("requested_name"), RequestedName);
	}
	return EventObj;
}

TSharedPtr<FJsonObject> BuildDeleteEvent(
	const FString& Mode,
	const FString& PlanId,
	const int32 StepIndex,
	const FString& Action,
	const FString& ActorName)
{
	TSharedPtr<FJsonObject> EventObj = BuildBaseEvent(
		TEXT("delete"),
		Mode,
		Action,
		TEXT("success"),
		PlanId,
		StepIndex);
	if (!ActorName.IsEmpty())
	{
		EventObj->SetStringField(TEXT("actor_name"), ActorName);
	}
	return EventObj;
}
} // namespace NovaBridgeCore
