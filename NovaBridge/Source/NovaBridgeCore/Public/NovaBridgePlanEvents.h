#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NovaBridgeCore
{
NOVABRIDGECORE_API TSharedPtr<FJsonObject> BuildPlanStepEvent(
	const FString& Mode,
	const FString& PlanId,
	const FString& Action,
	const TSharedPtr<FJsonObject>& StepResultObj);

NOVABRIDGECORE_API TSharedPtr<FJsonObject> BuildPlanCompleteEvent(
	const FString& Mode,
	const FString& PlanId,
	int32 StepCount,
	int32 SuccessCount,
	int32 ErrorCount,
	const FString& Role = FString());
} // namespace NovaBridgeCore

