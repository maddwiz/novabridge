#include "NovaBridgePlanDispatch.h"

namespace NovaBridgeCore
{
FString NormalizePlanAction(const FString& RawAction)
{
	FString Action = RawAction;
	Action.TrimStartAndEndInline();
	Action.ToLowerInline();
	return Action;
}

TSharedPtr<FJsonObject> MakePlanStepResult(const int32 StepIndex, const FString& Status, const FString& Message)
{
	TSharedPtr<FJsonObject> StepResult = MakeShared<FJsonObject>();
	StepResult->SetNumberField(TEXT("step"), StepIndex);
	StepResult->SetStringField(TEXT("status"), Status);
	StepResult->SetStringField(TEXT("message"), Message);
	return StepResult;
}

bool ExtractPlanStep(
	const TSharedPtr<FJsonValue>& StepValue,
	const int32 StepIndex,
	FPlanStepContext& OutStep,
	TSharedPtr<FJsonObject>& OutErrorResult)
{
	OutStep = FPlanStepContext();
	OutErrorResult.Reset();

	if (!StepValue.IsValid() || StepValue->Type != EJson::Object)
	{
		OutErrorResult = MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Step must be an object"));
		return false;
	}

	const TSharedPtr<FJsonObject> StepObj = StepValue->AsObject();
	if (!StepObj.IsValid() || !StepObj->HasTypedField<EJson::String>(TEXT("action")))
	{
		OutErrorResult = MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Missing step action"));
		return false;
	}

	const FString Action = NormalizePlanAction(StepObj->GetStringField(TEXT("action")));
	if (Action.IsEmpty())
	{
		OutErrorResult = MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Step action must be a non-empty string"));
		return false;
	}

	if (StepObj->HasField(TEXT("params")) && !StepObj->HasTypedField<EJson::Object>(TEXT("params")))
	{
		OutErrorResult = MakePlanStepResult(StepIndex, TEXT("error"), TEXT("Step params must be an object"));
		return false;
	}

	OutStep.StepIndex = StepIndex;
	OutStep.Action = Action;
	OutStep.Params = StepObj->HasTypedField<EJson::Object>(TEXT("params"))
		? StepObj->GetObjectField(TEXT("params"))
		: MakeShared<FJsonObject>();
	return true;
}

void FPlanCommandRouter::Register(const FString& Action, const FPlanCommandHandler& Handler)
{
	const FString NormalizedAction = NormalizePlanAction(Action);
	if (NormalizedAction.IsEmpty() || !Handler)
	{
		return;
	}

	Handlers.FindOrAdd(NormalizedAction) = Handler;
}

bool FPlanCommandRouter::HasHandler(const FString& Action) const
{
	const FString NormalizedAction = NormalizePlanAction(Action);
	return !NormalizedAction.IsEmpty() && Handlers.Contains(NormalizedAction);
}

TSharedPtr<FJsonObject> FPlanCommandRouter::Dispatch(const FPlanStepContext& Step) const
{
	const FString Action = NormalizePlanAction(Step.Action);
	if (Action.IsEmpty())
	{
		return MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Step action is empty"));
	}

	const FPlanCommandHandler* Handler = Handlers.Find(Action);
	if (!Handler || !(*Handler))
	{
		return MakePlanStepResult(
			Step.StepIndex,
			TEXT("error"),
			FString::Printf(TEXT("Unsupported action: %s"), *Action));
	}

	TSharedPtr<FJsonObject> StepResult = (*Handler)(Step);
	if (!StepResult.IsValid())
	{
		return MakePlanStepResult(Step.StepIndex, TEXT("error"), TEXT("Command handler returned no result"));
	}

	if (!StepResult->HasTypedField<EJson::Number>(TEXT("step")))
	{
		StepResult->SetNumberField(TEXT("step"), Step.StepIndex);
	}

	FString Status = StepResult->HasTypedField<EJson::String>(TEXT("status"))
		? NormalizePlanAction(StepResult->GetStringField(TEXT("status")))
		: FString();
	if (Status.IsEmpty())
	{
		Status = TEXT("error");
		StepResult->SetStringField(TEXT("status"), Status);
	}

	if (!StepResult->HasTypedField<EJson::String>(TEXT("message")))
	{
		StepResult->SetStringField(TEXT("message"), Status == TEXT("success") ? TEXT("Step executed") : TEXT("Step failed"));
	}

	return StepResult;
}
} // namespace NovaBridgeCore
