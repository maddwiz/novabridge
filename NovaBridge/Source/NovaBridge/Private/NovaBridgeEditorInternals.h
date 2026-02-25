#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
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
const TArray<FString>& SupportedEventTypes();
const FString& GetNovaBridgeDefaultRole();
int32 GetNovaBridgeEditorMaxPlanSteps();
TArray<FNovaBridgeAuditEntry> GetAuditTrailSnapshot();
void GetPendingEventSnapshot(int32& OutPendingEvents, TArray<FString>& OutPendingTypes);

bool IsRouteAllowedForRole(const FString& Role, const FString& RoutePath, EHttpServerRequestVerbs Verb);
int32 GetRouteRateLimitPerMinute(const FString& Role, const FString& RoutePath);
int32 GetPlanSpawnLimit(const FString& Role);
bool IsPlanActionAllowedForRole(const FString& Role, const FString& Action);
bool IsSpawnClassAllowedForRole(const FString& Role, const FString& ClassName);
bool IsSpawnLocationInBounds(const FVector& Location);

void PushUndoEntry(const FNovaBridgeUndoEntry& Entry);
bool PopUndoEntry(FNovaBridgeUndoEntry& OutEntry);

void PushAuditEntry(const FString& Route, const FString& Action, const FString& Role, const FString& Status, const FString& Message);
void QueueEventObject(const TSharedPtr<FJsonObject>& EventObj);

AActor* FindActorByName(const FString& Name);
UClass* ResolveActorClassByName(const FString& InClassName);
bool SetActorPropertyValue(AActor* Actor, const FString& PropertyName, const FString& Value, FString& OutError);

void RegisterEditorCapabilities(uint32 InEventWsPort);
