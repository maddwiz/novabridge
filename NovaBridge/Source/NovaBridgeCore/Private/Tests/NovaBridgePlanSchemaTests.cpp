#include "NovaBridgePlanSchema.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

namespace
{
TSharedPtr<FJsonObject> MakePlan(const FString& Action, const TSharedPtr<FJsonObject>& Params = nullptr)
{
	TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
	Step->SetStringField(TEXT("action"), Action);
	Step->SetObjectField(TEXT("params"), Params.IsValid() ? Params : MakeShared<FJsonObject>());

	TArray<TSharedPtr<FJsonValue>> Steps;
	Steps.Add(MakeShared<FJsonValueObject>(Step));

	TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
	Plan->SetStringField(TEXT("plan_id"), TEXT("schema-test"));
	Plan->SetArrayField(TEXT("steps"), Steps);
	return Plan;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaValidEditorPlan,
	"NovaBridge.Core.PlanSchema.ValidEditorPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaValidEditorPlan::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TSharedPtr<FJsonObject> SpawnParams = MakeShared<FJsonObject>();
	SpawnParams->SetStringField(TEXT("type"), TEXT("PointLight"));
	SpawnParams->SetStringField(TEXT("label"), TEXT("SchemaLight"));

	TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> LocationValues;
	LocationValues.Add(MakeShared<FJsonValueNumber>(0.0));
	LocationValues.Add(MakeShared<FJsonValueNumber>(25.0));
	LocationValues.Add(MakeShared<FJsonValueNumber>(310.0));
	Transform->SetArrayField(TEXT("location"), LocationValues);
	SpawnParams->SetObjectField(TEXT("transform"), Transform);

	TSharedPtr<FJsonObject> Plan = MakePlan(TEXT("spawn"), SpawnParams);

	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(
		Plan,
		NovaBridgeCore::ENovaBridgePlanMode::Editor,
		16,
		Error);

	TestTrue(TEXT("Editor spawn plan should validate"), bValid);
	TestEqual(TEXT("Error should stay empty"), Error.Message, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaRejectsUnknownTopLevelField,
	"NovaBridge.Core.PlanSchema.RejectsUnknownTopLevelField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaRejectsUnknownTopLevelField::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TSharedPtr<FJsonObject> Plan = MakePlan(TEXT("spawn"));
	Plan->SetStringField(TEXT("unknown_top"), TEXT("bad"));

	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(
		Plan,
		NovaBridgeCore::ENovaBridgePlanMode::Editor,
		16,
		Error);

	TestFalse(TEXT("Plan with unknown top-level field should fail"), bValid);
	TestTrue(TEXT("Error should identify unknown plan field"), Error.Message.Contains(TEXT("Unknown plan field: unknown_top")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaRejectsRuntimeScreenshotAction,
	"NovaBridge.Core.PlanSchema.RejectsRuntimeScreenshotAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaRejectsRuntimeScreenshotAction::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TSharedPtr<FJsonObject> Plan = MakePlan(TEXT("screenshot"));

	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(
		Plan,
		NovaBridgeCore::ENovaBridgePlanMode::Runtime,
		16,
		Error);

	TestFalse(TEXT("Runtime screenshot action should fail schema validation"), bValid);
	TestTrue(TEXT("Error should identify unsupported action"), Error.Message.Contains(TEXT("Unsupported action")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNovaBridgePlanSchemaRejectsInvalidSpawnTransform,
	"NovaBridge.Core.PlanSchema.RejectsInvalidSpawnTransform",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNovaBridgePlanSchemaRejectsInvalidSpawnTransform::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TSharedPtr<FJsonObject> SpawnParams = MakeShared<FJsonObject>();
	SpawnParams->SetStringField(TEXT("type"), TEXT("PointLight"));

	TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
	Transform->SetStringField(TEXT("location"), TEXT("not-a-vector"));
	SpawnParams->SetObjectField(TEXT("transform"), Transform);

	TSharedPtr<FJsonObject> Plan = MakePlan(TEXT("spawn"), SpawnParams);

	NovaBridgeCore::FPlanSchemaError Error;
	const bool bValid = NovaBridgeCore::ValidateExecutePlanSchema(
		Plan,
		NovaBridgeCore::ENovaBridgePlanMode::Editor,
		16,
		Error);

	TestFalse(TEXT("Invalid spawn transform.location should fail validation"), bValid);
	TestTrue(TEXT("Error should mention transform location shape"), Error.Message.Contains(TEXT("spawn.params.transform.location")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
