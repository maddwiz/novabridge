#include "NovaBridgeCapabilityRegistry.h"

#include "Dom/JsonValue.h"
#include "Misc/ScopeLock.h"

namespace NovaBridgeCore
{
FCapabilityRegistry& FCapabilityRegistry::Get()
{
	static FCapabilityRegistry Registry;
	return Registry;
}

void FCapabilityRegistry::Reset()
{
	FScopeLock Lock(&Mutex);
	Capabilities.Empty();
}

void FCapabilityRegistry::RegisterCapability(const FCapabilityRecord& Capability)
{
	if (Capability.Action.IsEmpty())
	{
		return;
	}

	FCapabilityRecord Stored;
	Stored.Action = Capability.Action;
	Stored.Roles = Capability.Roles;
	Stored.Data = MakeShared<FJsonObject>();
	if (Capability.Data.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Capability.Data->Values)
		{
			Stored.Data->SetField(Pair.Key, Pair.Value);
		}
	}

	FScopeLock Lock(&Mutex);
	Capabilities.Add(MoveTemp(Stored));
}

TArray<FCapabilityRecord> FCapabilityRegistry::Snapshot() const
{
	FScopeLock Lock(&Mutex);
	return Capabilities;
}

TSharedPtr<FJsonObject> CapabilityToJson(const FCapabilityRecord& Capability)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("action"), Capability.Action);

	if (Capability.Roles.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> RoleValues;
		RoleValues.Reserve(Capability.Roles.Num());
		for (const FString& Role : Capability.Roles)
		{
			RoleValues.Add(MakeShared<FJsonValueString>(Role));
		}
		JsonObject->SetArrayField(TEXT("roles"), RoleValues);
	}

	if (Capability.Data.IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Capability.Data->Values)
		{
			JsonObject->SetField(Pair.Key, Pair.Value);
		}
	}

	return JsonObject;
}
} // namespace NovaBridgeCore
