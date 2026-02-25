#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NovaBridgeCore
{
struct NOVABRIDGECORE_API FCapabilityRecord
{
	FString Action;
	TArray<FString> Roles;
	TSharedPtr<FJsonObject> Data;
};

class NOVABRIDGECORE_API FCapabilityRegistry
{
public:
	static FCapabilityRegistry& Get();

	void Reset();
	void RegisterCapability(const FCapabilityRecord& Capability);
	TArray<FCapabilityRecord> Snapshot() const;

private:
	mutable FCriticalSection Mutex;
	TArray<FCapabilityRecord> Capabilities;
};

NOVABRIDGECORE_API TSharedPtr<FJsonObject> CapabilityToJson(const FCapabilityRecord& Capability);
} // namespace NovaBridgeCore
