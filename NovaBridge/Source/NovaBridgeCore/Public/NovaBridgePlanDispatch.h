#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace NovaBridgeCore
{
struct NOVABRIDGECORE_API FPlanStepContext
{
	int32 StepIndex = INDEX_NONE;
	FString Action;
	TSharedPtr<FJsonObject> Params;
};

using FPlanCommandHandler = TFunction<TSharedPtr<FJsonObject>(const FPlanStepContext&)>;

class NOVABRIDGECORE_API FPlanCommandRouter
{
public:
	void Register(const FString& Action, const FPlanCommandHandler& Handler);
	bool HasHandler(const FString& Action) const;
	TSharedPtr<FJsonObject> Dispatch(const FPlanStepContext& Step) const;

private:
	TMap<FString, FPlanCommandHandler> Handlers;
};

NOVABRIDGECORE_API FString NormalizePlanAction(const FString& RawAction);
NOVABRIDGECORE_API TSharedPtr<FJsonObject> MakePlanStepResult(int32 StepIndex, const FString& Status, const FString& Message);
NOVABRIDGECORE_API bool ExtractPlanStep(
	const TSharedPtr<FJsonValue>& StepValue,
	int32 StepIndex,
	FPlanStepContext& OutStep,
	TSharedPtr<FJsonObject>& OutErrorResult);
} // namespace NovaBridgeCore
