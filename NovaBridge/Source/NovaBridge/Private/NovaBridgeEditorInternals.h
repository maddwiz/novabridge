#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpServerRequest.h"

class AActor;
class UClass;

struct FNovaBridgeUndoEntry
{
	FString Action;
	FString ActorName;
	FString ActorLabel;
};

struct FNovaBridgeAuditEntry
{
	FString TimestampUtc;
	FString Route;
	FString Action;
	FString Role;
	FString Status;
	FString Message;
};

FString ResolveRoleFromRequest(const FHttpServerRequest& Request);
void SetNovaBridgeDefaultRole(const FString& InRole);
const TArray<FString>& SupportedEventTypes();
const FString& GetNovaBridgeDefaultRole();
int32 GetNovaBridgeEditorMaxPlanSteps();
void ResetNovaBridgeEditorControlState();
TArray<FNovaBridgeAuditEntry> GetAuditTrailSnapshot();
void GetPendingEventSnapshot(int32& OutPendingEvents, TArray<FString>& OutPendingTypes);
void DrainPendingEventQueue(TArray<FString>& OutPendingPayloads, TArray<FString>& OutPendingTypes);

bool IsRouteAllowedForRole(const FString& Role, const FString& RoutePath, EHttpServerRequestVerbs Verb);
int32 GetRouteRateLimitPerMinute(const FString& Role, const FString& RoutePath);
int32 GetPlanSpawnLimit(const FString& Role);
bool IsPlanActionAllowedForRole(const FString& Role, const FString& Action);
bool ConsumeRateLimit(const FString& BucketKey, int32 LimitPerMinute, FString& OutError);
bool IsSpawnClassAllowedForRole(const FString& Role, const FString& ClassName);
bool IsSpawnLocationInBounds(const FVector& Location);
bool JsonValueToVector(const TSharedPtr<FJsonValue>& Value, FVector& OutVector);
bool JsonValueToRotator(const TSharedPtr<FJsonValue>& Value, FRotator& OutRotator);
bool JsonValueToImportText(const TSharedPtr<FJsonValue>& Value, FString& OutText);
void ParseSpawnTransformFromParams(const TSharedPtr<FJsonObject>& Params, FVector& OutLocation, FRotator& OutRotation, FVector& OutScale);

void PushUndoEntry(const FNovaBridgeUndoEntry& Entry);
bool PopUndoEntry(FNovaBridgeUndoEntry& OutEntry);

void PushAuditEntry(const FString& Route, const FString& Action, const FString& Role, const FString& Status, const FString& Message);
void QueueEventObject(const TSharedPtr<FJsonObject>& EventObj);

AActor* FindActorByName(const FString& Name);
TSharedPtr<FJsonObject> ActorToJson(AActor* Actor);
UClass* ResolveActorClassByName(const FString& InClassName);
bool SetActorPropertyValue(AActor* Actor, const FString& PropertyName, const FString& Value, FString& OutError);

void RegisterEditorCapabilities(uint32 InEventWsPort);
