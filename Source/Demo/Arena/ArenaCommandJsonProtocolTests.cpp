// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ArenaCommandJsonProtocol.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArenaJsonProtocolValidCommandsTest,
	"Demo.Arena.JsonProtocol.ValidCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArenaJsonProtocolValidCommandsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FArenaCommand Command;
	EArenaCommandError ErrorCode = EArenaCommandError::None;
	FString Message;

	const FString MoveToPointJson = TEXT(
		"{\"version\":1,\"requestId\":\"move-1\",\"actorId\":\"alice\"," 
		"\"command\":\"move_to_point\",\"parameters\":{" 
		"\"targetId\":\"center\",\"movementMode\":\"run\"}}");
	TestTrue(
		TEXT("move_to_point parses"),
		FArenaCommandJsonProtocol::TryParseCommand(
			MoveToPointJson,
			Command,
			ErrorCode,
			Message));
	TestEqual(TEXT("move_to_point command type"), Command.CommandType, EArenaCommandType::MoveToPoint);
	TestEqual(TEXT("move_to_point target"), Command.TargetId, FName(TEXT("center")));
	TestEqual(TEXT("move_to_point movement mode"), Command.MovementMode, EArenaMovementMode::Run);

	const FString MoveToActorJson = TEXT(
		"{\"version\":1,\"requestId\":\"move-2\",\"actorId\":\"alice\"," 
		"\"command\":\"move_to_actor\",\"parameters\":{\"targetId\":\"bob\"}}");
	TestTrue(
		TEXT("move_to_actor parses"),
		FArenaCommandJsonProtocol::TryParseCommand(
			MoveToActorJson,
			Command,
			ErrorCode,
			Message));
	TestEqual(TEXT("move_to_actor command type"), Command.CommandType, EArenaCommandType::MoveToActor);
	TestEqual(TEXT("move_to_actor target"), Command.TargetId, FName(TEXT("bob")));
	TestEqual(TEXT("move_to_actor default movement mode"), Command.MovementMode, EArenaMovementMode::Walk);

	const FString SpawnJson = TEXT(
		"{\"version\":1,\"requestId\":\"spawn-1\",\"actorId\":\"alice\"," 
		"\"command\":\"spawn\"}");
	TestTrue(
		TEXT("spawn parses without optional parameters"),
		FArenaCommandJsonProtocol::TryParseCommand(
			SpawnJson,
			Command,
			ErrorCode,
			Message));
	TestEqual(TEXT("spawn command type"), Command.CommandType, EArenaCommandType::Spawn);
	TestEqual(TEXT("spawn display name defaults to actor ID"), Command.DisplayName.ToString(), FString(TEXT("alice")));
	TestTrue(TEXT("spawn display name color defaults to white"), Command.DisplayNameColor == FColor::White);

	const FString ColoredSpawnJson = TEXT(
		"{\"version\":1,\"requestId\":\"spawn-2\",\"actorId\":\"bob\","
		"\"command\":\"spawn\",\"parameters\":{"
		"\"displayName\":\"Bob\",\"displayNameColor\":\"#1e90ff\"}}");
	TestTrue(
		TEXT("spawn parses a Twitch display name color"),
		FArenaCommandJsonProtocol::TryParseCommand(
			ColoredSpawnJson,
			Command,
			ErrorCode,
			Message));
	TestTrue(TEXT("spawn display name color"), Command.DisplayNameColor == FColor(0x1e, 0x90, 0xff));

	const FString ApproachObjectJson = TEXT(
		"{\"version\":1,\"requestId\":\"approach-1\",\"actorId\":\"alice\"," 
		"\"command\":\"approach_object\",\"parameters\":{\"targetId\":\"crate\"}}");
	TestTrue(
		TEXT("approach_object parses with defaults"),
		FArenaCommandJsonProtocol::TryParseCommand(
			ApproachObjectJson,
			Command,
			ErrorCode,
			Message));
	TestEqual(TEXT("approach_object command type"), Command.CommandType, EArenaCommandType::ApproachObject);
	TestEqual(TEXT("default interaction point"), Command.InteractionPointId, FName(TEXT("default")));
	TestEqual(TEXT("default movement mode"), Command.MovementMode, EArenaMovementMode::Walk);

	const FString PlayActionJson = TEXT(
		"{\"version\":1,\"requestId\":\"action-1\",\"actorId\":\"alice\"," 
		"\"command\":\"play_action\",\"parameters\":{" 
		"\"actionId\":\"punch\",\"targetType\":\"participant\",\"targetId\":\"bob\"}}");
	TestTrue(
		TEXT("play_action parses with a participant target"),
		FArenaCommandJsonProtocol::TryParseCommand(
			PlayActionJson,
			Command,
			ErrorCode,
			Message));
	TestEqual(TEXT("play_action command type"), Command.CommandType, EArenaCommandType::PlayAction);
	TestEqual(TEXT("play_action action"), Command.ActionId, FName(TEXT("punch")));
	TestEqual(
		TEXT("play_action target type"),
		Command.ActionTargetType,
		EArenaActionTargetType::Participant);
	TestEqual(TEXT("play_action target"), Command.TargetId, FName(TEXT("bob")));

	const TArray<TPair<FString, EArenaCommandType>> ParameterlessCommands = {
		{TEXT("stop"), EArenaCommandType::Stop},
		{TEXT("leave"), EArenaCommandType::Leave}};
	for (const TPair<FString, EArenaCommandType>& Pair : ParameterlessCommands)
	{
		const FString Json = FString::Printf(
			TEXT("{\"version\":1,\"requestId\":\"%s-1\",\"actorId\":\"alice\",\"command\":\"%s\"}"),
			*Pair.Key,
			*Pair.Key);
		TestTrue(
			*FString::Printf(TEXT("%s parses"), *Pair.Key),
			FArenaCommandJsonProtocol::TryParseCommand(Json, Command, ErrorCode, Message));
		TestEqual(
			*FString::Printf(TEXT("%s command type"), *Pair.Key),
			Command.CommandType,
			Pair.Value);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArenaJsonProtocolInvalidMessagesTest,
	"Demo.Arena.JsonProtocol.InvalidMessages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArenaJsonProtocolInvalidMessagesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	auto ExpectError = [this](
		const FString& CaseName,
		const FString& Json,
		const EArenaCommandError ExpectedError)
	{
		FArenaCommand Command;
		EArenaCommandError ErrorCode = EArenaCommandError::None;
		FString Message;
		TestFalse(
			*CaseName,
			FArenaCommandJsonProtocol::TryParseCommand(Json, Command, ErrorCode, Message));
		TestEqual(*FString::Printf(TEXT("%s error code"), *CaseName), ErrorCode, ExpectedError);
		TestFalse(*FString::Printf(TEXT("%s has a message"), *CaseName), Message.IsEmpty());
	};

	ExpectError(
		TEXT("invalid JSON"),
		TEXT("not-json"),
		EArenaCommandError::InvalidRequest);
	ExpectError(
		TEXT("unsupported version"),
		TEXT("{\"version\":2,\"requestId\":\"bad-version\",\"actorId\":\"alice\",\"command\":\"stop\"}"),
		EArenaCommandError::UnsupportedVersion);
	ExpectError(
		TEXT("missing requestId"),
		TEXT("{\"version\":1,\"actorId\":\"alice\",\"command\":\"stop\"}"),
		EArenaCommandError::InvalidRequest);
	ExpectError(
		TEXT("unknown command"),
		TEXT("{\"version\":1,\"requestId\":\"unknown-1\",\"actorId\":\"alice\",\"command\":\"call_function\"}"),
		EArenaCommandError::UnsupportedCommand);
	ExpectError(
		TEXT("invalid movement mode"),
		TEXT("{\"version\":1,\"requestId\":\"bad-mode\",\"actorId\":\"alice\",\"command\":\"move_to_point\",\"parameters\":{\"targetId\":\"center\",\"movementMode\":\"fly\"}}"),
		EArenaCommandError::InvalidRequest);
	ExpectError(
		TEXT("invalid display name color"),
		TEXT("{\"version\":1,\"requestId\":\"bad-color\",\"actorId\":\"alice\",\"command\":\"spawn\",\"parameters\":{\"displayNameColor\":\"blue\"}}"),
		EArenaCommandError::InvalidRequest);
	ExpectError(
		TEXT("unknown parameter"),
		TEXT("{\"version\":1,\"requestId\":\"extra-1\",\"actorId\":\"alice\",\"command\":\"stop\",\"parameters\":{\"functionName\":\"QuitGame\"}}"),
		EArenaCommandError::InvalidRequest);
	ExpectError(
		TEXT("missing action target"),
		TEXT("{\"version\":1,\"requestId\":\"bad-target\",\"actorId\":\"alice\",\"command\":\"play_action\",\"parameters\":{\"actionId\":\"punch\",\"targetType\":\"participant\"}}"),
		EArenaCommandError::InvalidRequest);
	ExpectError(
		TEXT("oversized message"),
		FString::ChrN(FArenaCommandJsonProtocol::MaximumMessageLength + 1, TEXT('x')),
		EArenaCommandError::InvalidRequest);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FArenaJsonProtocolSerializationTest,
	"Demo.Arena.JsonProtocol.Serialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FArenaJsonProtocolSerializationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString CompletedJson = FArenaCommandJsonProtocol::SerializeResult(
		TEXT("42"),
		EArenaCommandStatus::Completed,
		EArenaCommandError::None,
		FString());
	TSharedPtr<FJsonObject> CompletedObject;
	const TSharedRef<TJsonReader<>> CompletedReader = TJsonReaderFactory<>::Create(CompletedJson);
	TestTrue(
		TEXT("completed response is valid JSON"),
		FJsonSerializer::Deserialize(CompletedReader, CompletedObject) && CompletedObject.IsValid());
	if (CompletedObject.IsValid())
	{
		TestEqual(TEXT("response version"), CompletedObject->GetIntegerField(TEXT("version")), 1);
		TestEqual(TEXT("response requestId"), CompletedObject->GetStringField(TEXT("requestId")), FString(TEXT("42")));
		TestEqual(TEXT("response status"), CompletedObject->GetStringField(TEXT("status")), FString(TEXT("completed")));
		const TSharedPtr<FJsonValue> ErrorValue = CompletedObject->TryGetField(TEXT("errorCode"));
		TestTrue(
			TEXT("successful response has a null errorCode"),
			ErrorValue.IsValid() && ErrorValue->Type == EJson::Null);
	}

	const FString RejectedJson = FArenaCommandJsonProtocol::SerializeResult(
		TEXT("43"),
		EArenaCommandStatus::Rejected,
		EArenaCommandError::UnknownTarget,
		TEXT("Target was not found."));
	TSharedPtr<FJsonObject> RejectedObject;
	const TSharedRef<TJsonReader<>> RejectedReader = TJsonReaderFactory<>::Create(RejectedJson);
	TestTrue(
		TEXT("rejected response is valid JSON"),
		FJsonSerializer::Deserialize(RejectedReader, RejectedObject) && RejectedObject.IsValid());
	if (RejectedObject.IsValid())
	{
		TestEqual(
			TEXT("rejected response error code"),
			RejectedObject->GetStringField(TEXT("errorCode")),
			FString(TEXT("unknown_target")));
		TestEqual(
			TEXT("rejected response message"),
			RejectedObject->GetStringField(TEXT("message")),
			FString(TEXT("Target was not found.")));
	}

	return true;
}

#endif
