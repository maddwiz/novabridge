#include "NovaBridgeCapabilityRegistry.h"
#include "NovaBridgePlanSchema.h"
#include "NovaBridgePolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace
{
TSharedPtr<FJsonObject> MakeSingleStepPlan(const FString& Action, const TSharedPtr<FJsonObject>& Params = nullptr)
{
	const TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
	Step->SetStringField(TEXT("action"), Action);
	Step->SetObjectField(TEXT("params"), Params.IsValid() ? Params : MakeShared<FJsonObject>());

	TArray<TSharedPtr<FJsonValue>> Steps;
	Steps.Add(MakeShared<FJsonValueObject>(Step));

	const TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
	Plan->SetStringField(TEXT("plan_id"), TEXT("nb-core-test"));
	Plan->SetArrayField(TEXT("steps"), Steps);
	return Plan;
}

TSharedPtr<FJsonObject> MakeCallParams(const bool bIncludeFunction = true)
{
	const TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("target"), TEXT("Actor_1"));
	if (bIncludeFunction)
	{
		Params->SetStringField(TEXT("function"), TEXT("Trigger"));
	}
	return Params;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaValidRuntimeCall,
	"NovaBridge.Core.PlanSchema.ValidRuntimeCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaValidRuntimeCall::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<FJsonObject> Plan = MakeSingleStepPlan(TEXT("call"), MakeCallParams(true));
	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(Plan, NovaBridgeCore::ENovaBridgePlanMode::Runtime, 16, Error);
	TestTrue(TEXT("Runtime call action should validate"), bValid);
	TestEqual(TEXT("Error should stay empty"), Error.Message, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaRejectsCallWithoutFunction,
	"NovaBridge.Core.PlanSchema.RejectsCallWithoutFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaRejectsCallWithoutFunction::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<FJsonObject> Plan = MakeSingleStepPlan(TEXT("call"), MakeCallParams(false));
	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(Plan, NovaBridgeCore::ENovaBridgePlanMode::Runtime, 16, Error);
	TestFalse(TEXT("Runtime call action without function/event should fail"), bValid);
	TestTrue(TEXT("Error should mention required call function/event"), Error.Message.Contains(TEXT("call.params.function")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaRejectsUnknownCallField,
	"NovaBridge.Core.PlanSchema.RejectsUnknownCallField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaRejectsUnknownCallField::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<FJsonObject> Params = MakeCallParams(true);
	Params->SetStringField(TEXT("bad_field"), TEXT("x"));
	const TSharedPtr<FJsonObject> Plan = MakeSingleStepPlan(TEXT("call"), Params);
	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(Plan, NovaBridgeCore::ENovaBridgePlanMode::Runtime, 16, Error);
	TestFalse(TEXT("Unknown call field should fail"), bValid);
	TestTrue(TEXT("Error should mention unknown call field"), Error.Message.Contains(TEXT("Unknown call param field")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaValidCallArgsArray,
	"NovaBridge.Core.PlanSchema.ValidCallArgsArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaValidCallArgsArray::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<FJsonObject> Params = MakeCallParams(true);
	TArray<TSharedPtr<FJsonValue>> Args;
	Args.Add(MakeShared<FJsonValueNumber>(5.0));
	Args.Add(MakeShared<FJsonValueString>(TEXT("hello")));
	Params->SetArrayField(TEXT("args"), Args);
	const TSharedPtr<FJsonObject> Plan = MakeSingleStepPlan(TEXT("call"), Params);

	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(Plan, NovaBridgeCore::ENovaBridgePlanMode::Runtime, 16, Error);
	TestTrue(TEXT("Runtime call with args array should pass"), bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaRejectsInvalidCallArgs,
	"NovaBridge.Core.PlanSchema.RejectsInvalidCallArgs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaRejectsInvalidCallArgs::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<FJsonObject> Params = MakeCallParams(true);
	Params->SetNumberField(TEXT("args"), 7.0);
	const TSharedPtr<FJsonObject> Plan = MakeSingleStepPlan(TEXT("call"), Params);

	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(Plan, NovaBridgeCore::ENovaBridgePlanMode::Runtime, 16, Error);
	TestFalse(TEXT("Runtime call with non-container args should fail"), bValid);
	TestTrue(TEXT("Error should mention args shape"), Error.Message.Contains(TEXT("call.params.args")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaRejectsEditorRoleInRuntimePlan,
	"NovaBridge.Core.PlanSchema.RejectsEditorRoleFieldInRuntimePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaRejectsEditorRoleInRuntimePlan::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<FJsonObject> Plan = MakeSingleStepPlan(TEXT("spawn"));
	Plan->SetStringField(TEXT("role"), TEXT("automation"));

	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(Plan, NovaBridgeCore::ENovaBridgePlanMode::Runtime, 16, Error);
	TestFalse(TEXT("Runtime plan should reject editor-only role field"), bValid);
	TestTrue(TEXT("Error should mention unknown plan field"), Error.Message.Contains(TEXT("Unknown plan field")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaRejectsTooManySteps,
	"NovaBridge.Core.PlanSchema.RejectsTooManySteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaRejectsTooManySteps::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
	Plan->SetStringField(TEXT("plan_id"), TEXT("nb-many-steps"));
	TArray<TSharedPtr<FJsonValue>> Steps;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("action"), TEXT("spawn"));
		Step->SetObjectField(TEXT("params"), MakeShared<FJsonObject>());
		Steps.Add(MakeShared<FJsonValueObject>(Step));
	}
	Plan->SetArrayField(TEXT("steps"), Steps);

	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(Plan, NovaBridgeCore::ENovaBridgePlanMode::Runtime, 2, Error);
	TestFalse(TEXT("Plan with too many steps should fail"), bValid);
	TestTrue(TEXT("Error should mention max step count"), Error.Message.Contains(TEXT("max step count")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgeCapabilityRegistryRegistersAndResets,
	"NovaBridge.Core.CapabilityRegistry.RegistersAndResets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgeCapabilityRegistryRegistersAndResets::RunTest(const FString& Parameters)
{
	(void)Parameters;
	NovaBridgeCore::FCapabilityRegistry& Registry = NovaBridgeCore::FCapabilityRegistry::Get();
	Registry.Reset();

	NovaBridgeCore::FCapabilityRecord Capability;
	Capability.Action = TEXT("scene.list");
	Capability.Roles = { TEXT("admin"), TEXT("read_only") };
	Capability.Data = MakeShared<FJsonObject>();
	Capability.Data->SetStringField(TEXT("endpoint"), TEXT("/nova/scene/list"));
	Registry.RegisterCapability(Capability);

	TArray<NovaBridgeCore::FCapabilityRecord> Snapshot = Registry.Snapshot();
	TestEqual(TEXT("Registry should contain one capability"), Snapshot.Num(), 1);
	TestEqual(TEXT("Capability action should match"), Snapshot[0].Action, FString(TEXT("scene.list")));

	Registry.Reset();
	Snapshot = Registry.Snapshot();
	TestEqual(TEXT("Registry reset should clear capabilities"), Snapshot.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgeCapabilityRegistryDeepCopiesData,
	"NovaBridge.Core.CapabilityRegistry.DeepCopiesData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgeCapabilityRegistryDeepCopiesData::RunTest(const FString& Parameters)
{
	(void)Parameters;
	NovaBridgeCore::FCapabilityRegistry& Registry = NovaBridgeCore::FCapabilityRegistry::Get();
	Registry.Reset();

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("endpoint"), TEXT("/nova/test"));

	NovaBridgeCore::FCapabilityRecord Capability;
	Capability.Action = TEXT("test.action");
	Capability.Data = Data;
	Registry.RegisterCapability(Capability);

	Data->SetStringField(TEXT("endpoint"), TEXT("/nova/changed"));
	const TArray<NovaBridgeCore::FCapabilityRecord> Snapshot = Registry.Snapshot();
	TestEqual(TEXT("Stored data should remain immutable"), Snapshot[0].Data->GetStringField(TEXT("endpoint")), FString(TEXT("/nova/test")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgeCapabilityRoleFilterHonorsRoleList,
	"NovaBridge.Core.CapabilityRegistry.RoleFilterHonorsRoleList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgeCapabilityRoleFilterHonorsRoleList::RunTest(const FString& Parameters)
{
	(void)Parameters;
	NovaBridgeCore::FCapabilityRecord Capability;
	Capability.Action = TEXT("executePlan");
	Capability.Roles = { TEXT("admin"), TEXT("automation") };
	Capability.Data = MakeShared<FJsonObject>();

	TestTrue(TEXT("Admin should be allowed"), NovaBridgeCore::IsCapabilityAllowedForRole(Capability, TEXT("admin")));
	TestTrue(TEXT("Automation should be allowed"), NovaBridgeCore::IsCapabilityAllowedForRole(Capability, TEXT("automation")));
	TestFalse(TEXT("Read-only should be denied"), NovaBridgeCore::IsCapabilityAllowedForRole(Capability, TEXT("read_only")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgeRuntimePolicySpawnLimitsByRole,
	"NovaBridge.Core.Policy.RuntimeSpawnLimitsByRole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgeRuntimePolicySpawnLimitsByRole::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("Admin spawn limit"), NovaBridgeCore::GetRuntimePlanSpawnLimit(TEXT("admin")), 100);
	TestEqual(TEXT("Automation spawn limit"), NovaBridgeCore::GetRuntimePlanSpawnLimit(TEXT("automation")), 25);
	TestEqual(TEXT("Read-only spawn limit"), NovaBridgeCore::GetRuntimePlanSpawnLimit(TEXT("read_only")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgeRuntimePolicyActionPermissions,
	"NovaBridge.Core.Policy.RuntimeActionPermissions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgeRuntimePolicyActionPermissions::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Admin can call"), NovaBridgeCore::IsRuntimePlanActionAllowedForRole(TEXT("admin"), TEXT("call")));
	TestTrue(TEXT("Automation can screenshot"), NovaBridgeCore::IsRuntimePlanActionAllowedForRole(TEXT("automation"), TEXT("screenshot")));
	TestFalse(TEXT("Read-only cannot set"), NovaBridgeCore::IsRuntimePlanActionAllowedForRole(TEXT("read_only"), TEXT("set")));
	TestFalse(TEXT("Unknown role denied"), NovaBridgeCore::IsRuntimePlanActionAllowedForRole(TEXT("guest"), TEXT("spawn")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
