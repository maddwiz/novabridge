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

NOVABRIDGECORE_API TSharedPtr<FJsonObject> BuildSpawnEvent(
	const FString& Mode,
	const FString& PlanId,
	int32 StepIndex,
	const FString& Action,
	const FString& ObjectId,
	const FString& ClassName = FString(),
	const FString& ActorName = FString(),
	const FString& ActorLabel = FString(),
	const FString& RequestedName = FString());

NOVABRIDGECORE_API TSharedPtr<FJsonObject> BuildDeleteEvent(
	const FString& Mode,
	const FString& PlanId,
	int32 StepIndex,
	const FString& Action,
	const FString& ActorName);
} // namespace NovaBridgeCore
