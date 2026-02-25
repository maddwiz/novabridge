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

	TSharedPtr<FJsonObject> EventObj = MakeShared<FJsonObject>();
	EventObj->SetStringField(TEXT("type"), Status.Equals(TEXT("error"), ESearchCase::IgnoreCase) ? TEXT("error") : TEXT("plan_step"));
	EventObj->SetStringField(TEXT("mode"), Mode);
	EventObj->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	EventObj->SetStringField(TEXT("route"), TEXT("/nova/executePlan"));
	EventObj->SetStringField(TEXT("action"), Action.IsEmpty() ? TEXT("unknown") : Action);
	EventObj->SetStringField(TEXT("status"), Status.IsEmpty() ? TEXT("error") : Status);
	EventObj->SetStringField(TEXT("plan_id"), PlanId);
	EventObj->SetNumberField(TEXT("step"), StepIndex);

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
	TSharedPtr<FJsonObject> EventObj = MakeShared<FJsonObject>();
	EventObj->SetStringField(TEXT("type"), TEXT("plan_complete"));
	EventObj->SetStringField(TEXT("mode"), Mode);
	EventObj->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	EventObj->SetStringField(TEXT("route"), TEXT("/nova/executePlan"));
	EventObj->SetStringField(TEXT("action"), TEXT("executePlan.complete"));
	EventObj->SetStringField(TEXT("status"), ErrorCount == 0 ? TEXT("success") : TEXT("partial"));
	EventObj->SetStringField(TEXT("plan_id"), PlanId);
	if (!Role.IsEmpty())
	{
		EventObj->SetStringField(TEXT("role"), Role);
	}
	EventObj->SetNumberField(TEXT("step_count"), StepCount);
	EventObj->SetNumberField(TEXT("success_count"), SuccessCount);
	EventObj->SetNumberField(TEXT("error_count"), ErrorCount);
	return EventObj;
}
} // namespace NovaBridgeCore

