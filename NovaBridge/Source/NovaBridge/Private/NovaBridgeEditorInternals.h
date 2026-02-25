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

FString ResolveRoleFromRequest(const FHttpServerRequest& Request);
bool IsSpawnClassAllowedForRole(const FString& Role, const FString& ClassName);
bool IsSpawnLocationInBounds(const FVector& Location);

void PushUndoEntry(const FNovaBridgeUndoEntry& Entry);
bool PopUndoEntry(FNovaBridgeUndoEntry& OutEntry);

void PushAuditEntry(const FString& Route, const FString& Action, const FString& Role, const FString& Status, const FString& Message);
void QueueEventObject(const TSharedPtr<FJsonObject>& EventObj);

UClass* ResolveActorClassByName(const FString& InClassName);
bool SetActorPropertyValue(AActor* Actor, const FString& PropertyName, const FString& Value, FString& OutError);
