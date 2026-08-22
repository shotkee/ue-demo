// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaCommandJsonProtocol.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ArenaCommandJsonProtocol
{
	const FString VersionField = TEXT("version");
	const FString RequestIdField = TEXT("requestId");
	const FString ActorIdField = TEXT("actorId");
	const FString CommandField = TEXT("command");
	const FString ParametersField = TEXT("parameters");

	const FString DisplayNameField = TEXT("displayName");
	const FString DisplayNameColorField = TEXT("displayNameColor");
	const FString TargetIdField = TEXT("targetId");
	const FString InteractionPointIdField = TEXT("interactionPointId");
	const FString ActionIdField = TEXT("actionId");
	const FString TargetTypeField = TEXT("targetType");
	const FString MovementModeField = TEXT("movementMode");

	bool SetParseError(
		const EArenaCommandError ErrorCode,
		const FString& Message,
		EArenaCommandError& OutErrorCode,
		FString& OutMessage)
	{
		OutErrorCode = ErrorCode;
		OutMessage = Message;
		return false;
	}

	TSharedPtr<FJsonValue> FindField(const FJsonObject& Object, const FString& FieldName)
	{
		return Object.TryGetField(FieldName);
	}

	bool TryReadString(
		const FJsonObject& Object,
		const FString& FieldName,
		const bool bRequired,
		const int32 MaximumLength,
		FString& OutValue,
		bool& bOutWasPresent,
		FString& OutMessage)
	{
		bOutWasPresent = false;
		OutValue.Reset();

		const TSharedPtr<FJsonValue> JsonValue = FindField(Object, FieldName);
		if (!JsonValue.IsValid())
		{
			if (bRequired)
			{
				OutMessage = FString::Printf(TEXT("Field '%s' is required."), *FieldName);
				return false;
			}
			return true;
		}

		bOutWasPresent = true;
		if (JsonValue->Type != EJson::String)
		{
			OutMessage = FString::Printf(TEXT("Field '%s' must be a string."), *FieldName);
			return false;
		}

		OutValue = JsonValue->AsString();
		OutValue.TrimStartAndEndInline();
		if (OutValue.IsEmpty())
		{
			OutMessage = FString::Printf(TEXT("Field '%s' cannot be empty."), *FieldName);
			return false;
		}

		if (OutValue.Len() > MaximumLength)
		{
			OutMessage = FString::Printf(
				TEXT("Field '%s' exceeds the maximum length of %d characters."),
				*FieldName,
				MaximumLength);
			return false;
		}

		return true;
	}

	bool TryReadRequiredString(
		const FJsonObject& Object,
		const FString& FieldName,
		const int32 MaximumLength,
		FString& OutValue,
		FString& OutMessage)
	{
		bool bWasPresent = false;
		return TryReadString(
			Object,
			FieldName,
			true,
			MaximumLength,
			OutValue,
			bWasPresent,
			OutMessage);
	}

	bool TryReadOptionalString(
		const FJsonObject& Object,
		const FString& FieldName,
		const int32 MaximumLength,
		FString& OutValue,
		bool& bOutWasPresent,
		FString& OutMessage)
	{
		return TryReadString(
			Object,
			FieldName,
			false,
			MaximumLength,
			OutValue,
			bOutWasPresent,
			OutMessage);
	}

	bool HasOnlyFields(
		const FJsonObject& Object,
		const TSet<FString>& AllowedFields,
		FString& OutMessage)
	{
		int32 RecognizedFieldCount = 0;
		for (const FString& AllowedField : AllowedFields)
		{
			if (Object.HasField(AllowedField))
			{
				++RecognizedFieldCount;
			}
		}

		if (RecognizedFieldCount == Object.Values.Num())
		{
			return true;
		}

		OutMessage = TEXT("JSON object contains an unknown field.");
		return false;
	}

	bool TryReadParameters(
		const FJsonObject& RootObject,
		TSharedPtr<FJsonObject>& OutParameters,
		FString& OutMessage)
	{
		const TSharedPtr<FJsonValue> ParametersValue = FindField(RootObject, ParametersField);
		if (!ParametersValue.IsValid())
		{
			OutParameters = MakeShared<FJsonObject>();
			return true;
		}

		if (ParametersValue->Type != EJson::Object)
		{
			OutMessage = TEXT("Field 'parameters' must be an object.");
			return false;
		}

		OutParameters = ParametersValue->AsObject();
		if (!OutParameters.IsValid())
		{
			OutMessage = TEXT("Field 'parameters' must be an object.");
			return false;
		}

		return true;
	}

	bool TryReadMovementMode(
		const FJsonObject& Parameters,
		EArenaMovementMode& OutMovementMode,
		FString& OutMessage)
	{
		FString MovementMode;
		bool bWasPresent = false;
		if (!TryReadOptionalString(
			Parameters,
			MovementModeField,
			16,
			MovementMode,
			bWasPresent,
			OutMessage))
		{
			return false;
		}

		if (!bWasPresent || MovementMode == TEXT("walk"))
		{
			OutMovementMode = EArenaMovementMode::Walk;
			return true;
		}

		if (MovementMode == TEXT("run"))
		{
			OutMovementMode = EArenaMovementMode::Run;
			return true;
		}

		OutMessage = TEXT("Field 'movementMode' must be 'walk' or 'run'.");
		return false;
	}

	FString StatusToString(const EArenaCommandStatus Status)
	{
		switch (Status)
		{
		case EArenaCommandStatus::Received:
			return TEXT("received");
		case EArenaCommandStatus::Accepted:
			return TEXT("accepted");
		case EArenaCommandStatus::Started:
			return TEXT("started");
		case EArenaCommandStatus::Completed:
			return TEXT("completed");
		case EArenaCommandStatus::Rejected:
			return TEXT("rejected");
		case EArenaCommandStatus::Failed:
			return TEXT("failed");
		case EArenaCommandStatus::Cancelled:
			return TEXT("cancelled");
		default:
			return TEXT("failed");
		}
	}

	FString ErrorToString(const EArenaCommandError ErrorCode)
	{
		switch (ErrorCode)
		{
		case EArenaCommandError::InvalidRequest:
			return TEXT("invalid_request");
		case EArenaCommandError::UnsupportedVersion:
			return TEXT("unsupported_version");
		case EArenaCommandError::DuplicateRequestId:
			return TEXT("duplicate_request_id");
		case EArenaCommandError::DuplicateParticipant:
			return TEXT("duplicate_participant");
		case EArenaCommandError::UnknownParticipant:
			return TEXT("unknown_participant");
		case EArenaCommandError::UnknownTarget:
			return TEXT("unknown_target");
		case EArenaCommandError::UnknownAction:
			return TEXT("unknown_action");
		case EArenaCommandError::ActionNotAllowed:
			return TEXT("action_not_allowed");
		case EArenaCommandError::ActionUnavailable:
			return TEXT("action_unavailable");
		case EArenaCommandError::ActionInterrupted:
			return TEXT("action_interrupted");
		case EArenaCommandError::UnreachableTarget:
			return TEXT("unreachable_target");
		case EArenaCommandError::QueueFull:
			return TEXT("queue_full");
		case EArenaCommandError::UnsupportedCommand:
			return TEXT("unsupported_command");
		case EArenaCommandError::ExecutionFailed:
			return TEXT("execution_failed");
		case EArenaCommandError::CancelledByStop:
			return TEXT("cancelled_by_stop");
		case EArenaCommandError::ParticipantRemoved:
			return TEXT("participant_removed");
		case EArenaCommandError::None:
		default:
			return FString();
		}
	}
}

bool FArenaCommandJsonProtocol::TryParseCommand(
	const FString& JsonMessage,
	FArenaCommand& OutCommand,
	EArenaCommandError& OutErrorCode,
	FString& OutMessage)
{
	using namespace ArenaCommandJsonProtocol;

	OutCommand = FArenaCommand();
	OutErrorCode = EArenaCommandError::None;
	OutMessage.Reset();

	if (JsonMessage.Len() > MaximumMessageLength)
	{
		return SetParseError(
			EArenaCommandError::InvalidRequest,
			FString::Printf(
				TEXT("JSON message exceeds the maximum length of %d characters."),
				MaximumMessageLength),
			OutErrorCode,
			OutMessage);
	}

	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonMessage);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return SetParseError(
			EArenaCommandError::InvalidRequest,
			TEXT("Message must contain one valid JSON object."),
			OutErrorCode,
			OutMessage);
	}

	FString FieldError;
	bool bRequestIdWasPresent = false;
	TryReadOptionalString(
		*RootObject,
		RequestIdField,
		MaximumIdentifierLength,
		OutCommand.RequestId,
		bRequestIdWasPresent,
		FieldError);

	bool bActorIdWasPresent = false;
	TryReadOptionalString(
		*RootObject,
		ActorIdField,
		MaximumIdentifierLength,
		OutCommand.ActorId,
		bActorIdWasPresent,
		FieldError);

	const TSet<FString> AllowedRootFields = {
		VersionField,
		RequestIdField,
		ActorIdField,
		CommandField,
		ParametersField};
	if (!HasOnlyFields(*RootObject, AllowedRootFields, FieldError))
	{
		return SetParseError(
			EArenaCommandError::InvalidRequest,
			FieldError,
			OutErrorCode,
			OutMessage);
	}

	const TSharedPtr<FJsonValue> VersionValue = FindField(*RootObject, VersionField);
	if (!VersionValue.IsValid() || VersionValue->Type != EJson::Number)
	{
		return SetParseError(
			EArenaCommandError::InvalidRequest,
			TEXT("Field 'version' is required and must be an integer."),
			OutErrorCode,
			OutMessage);
	}

	const double VersionNumber = VersionValue->AsNumber();
	if (!FMath::IsFinite(VersionNumber)
		|| VersionNumber != FMath::RoundToDouble(VersionNumber)
		|| VersionNumber < MIN_int32
		|| VersionNumber > MAX_int32)
	{
		return SetParseError(
			EArenaCommandError::InvalidRequest,
			TEXT("Field 'version' is required and must be an integer."),
			OutErrorCode,
			OutMessage);
	}

	OutCommand.Version = static_cast<int32>(VersionNumber);
	if (OutCommand.Version != ProtocolVersion)
	{
		return SetParseError(
			EArenaCommandError::UnsupportedVersion,
			FString::Printf(
				TEXT("Unsupported protocol version %d. Expected version %d."),
				OutCommand.Version,
				ProtocolVersion),
			OutErrorCode,
			OutMessage);
	}

	if (!TryReadRequiredString(
		*RootObject,
		RequestIdField,
		MaximumIdentifierLength,
		OutCommand.RequestId,
		FieldError))
	{
		return SetParseError(
			EArenaCommandError::InvalidRequest,
			FieldError,
			OutErrorCode,
			OutMessage);
	}

	if (!TryReadRequiredString(
		*RootObject,
		ActorIdField,
		MaximumIdentifierLength,
		OutCommand.ActorId,
		FieldError))
	{
		return SetParseError(
			EArenaCommandError::InvalidRequest,
			FieldError,
			OutErrorCode,
			OutMessage);
	}

	FString CommandName;
	if (!TryReadRequiredString(
		*RootObject,
		CommandField,
		32,
		CommandName,
		FieldError))
	{
		return SetParseError(
			EArenaCommandError::InvalidRequest,
			FieldError,
			OutErrorCode,
			OutMessage);
	}

	TSharedPtr<FJsonObject> Parameters;
	if (!TryReadParameters(*RootObject, Parameters, FieldError))
	{
		return SetParseError(
			EArenaCommandError::InvalidRequest,
			FieldError,
			OutErrorCode,
			OutMessage);
	}

	if (CommandName == TEXT("spawn"))
	{
		OutCommand.CommandType = EArenaCommandType::Spawn;
		const TSet<FString> AllowedFields = {DisplayNameField, DisplayNameColorField};
		if (!HasOnlyFields(*Parameters, AllowedFields, FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		FString DisplayName;
		bool bDisplayNameWasPresent = false;
		if (!TryReadOptionalString(
			*Parameters,
			DisplayNameField,
			MaximumDisplayNameLength,
			DisplayName,
			bDisplayNameWasPresent,
			FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		OutCommand.DisplayName = FText::FromString(
			bDisplayNameWasPresent ? DisplayName : OutCommand.ActorId);

		FString DisplayNameColor;
		bool bDisplayNameColorWasPresent = false;
		if (!TryReadOptionalString(
			*Parameters,
			DisplayNameColorField,
			7,
			DisplayNameColor,
			bDisplayNameColorWasPresent,
			FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		if (bDisplayNameColorWasPresent)
		{
			bool bIsValidColor = DisplayNameColor.Len() == 7 && DisplayNameColor[0] == TEXT('#');
			for (int32 Index = 1; bIsValidColor && Index < DisplayNameColor.Len(); ++Index)
			{
				const TCHAR Character = DisplayNameColor[Index];
				bIsValidColor = (Character >= TEXT('0') && Character <= TEXT('9'))
					|| (Character >= TEXT('A') && Character <= TEXT('F'))
					|| (Character >= TEXT('a') && Character <= TEXT('f'));
			}
			if (!bIsValidColor)
			{
				return SetParseError(
					EArenaCommandError::InvalidRequest,
					TEXT("Field 'displayNameColor' must use the #RRGGBB format."),
					OutErrorCode,
					OutMessage);
			}
			OutCommand.DisplayNameColor = FColor::FromHex(DisplayNameColor);
			OutCommand.DisplayNameColor.A = 255;
		}
		return true;
	}

	if (CommandName == TEXT("move_to_point") || CommandName == TEXT("move_to_actor"))
	{
		OutCommand.CommandType = CommandName == TEXT("move_to_point")
			? EArenaCommandType::MoveToPoint
			: EArenaCommandType::MoveToActor;
		const TSet<FString> AllowedFields = {TargetIdField, MovementModeField};
		if (!HasOnlyFields(*Parameters, AllowedFields, FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		FString TargetId;
		if (!TryReadRequiredString(
			*Parameters,
			TargetIdField,
			MaximumIdentifierLength,
			TargetId,
			FieldError)
			|| !TryReadMovementMode(*Parameters, OutCommand.MovementMode, FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		OutCommand.TargetId = FName(*TargetId);
		return true;
	}

	if (CommandName == TEXT("approach_object"))
	{
		OutCommand.CommandType = EArenaCommandType::ApproachObject;
		const TSet<FString> AllowedFields = {
			TargetIdField,
			InteractionPointIdField,
			MovementModeField};
		if (!HasOnlyFields(*Parameters, AllowedFields, FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		FString TargetId;
		if (!TryReadRequiredString(
			*Parameters,
			TargetIdField,
			MaximumIdentifierLength,
			TargetId,
			FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		FString InteractionPointId;
		bool bInteractionPointWasPresent = false;
		if (!TryReadOptionalString(
			*Parameters,
			InteractionPointIdField,
			MaximumIdentifierLength,
			InteractionPointId,
			bInteractionPointWasPresent,
			FieldError)
			|| !TryReadMovementMode(*Parameters, OutCommand.MovementMode, FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		OutCommand.TargetId = FName(*TargetId);
		OutCommand.InteractionPointId = bInteractionPointWasPresent
			? FName(*InteractionPointId)
			: FName(TEXT("default"));
		return true;
	}

	if (CommandName == TEXT("play_action"))
	{
		OutCommand.CommandType = EArenaCommandType::PlayAction;
		const TSet<FString> AllowedFields = {ActionIdField, TargetTypeField, TargetIdField};
		if (!HasOnlyFields(*Parameters, AllowedFields, FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		FString ActionId;
		if (!TryReadRequiredString(
			*Parameters,
			ActionIdField,
			MaximumIdentifierLength,
			ActionId,
			FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		FString TargetType;
		bool bTargetTypeWasPresent = false;
		if (!TryReadOptionalString(
			*Parameters,
			TargetTypeField,
			32,
			TargetType,
			bTargetTypeWasPresent,
			FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		if (!bTargetTypeWasPresent || TargetType == TEXT("none"))
		{
			OutCommand.ActionTargetType = EArenaActionTargetType::None;
		}
		else if (TargetType == TEXT("participant"))
		{
			OutCommand.ActionTargetType = EArenaActionTargetType::Participant;
		}
		else if (TargetType == TEXT("arena_object"))
		{
			OutCommand.ActionTargetType = EArenaActionTargetType::ArenaObject;
		}
		else
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				TEXT("Field 'targetType' must be 'none', 'participant', or 'arena_object'."),
				OutErrorCode,
				OutMessage);
		}

		FString TargetId;
		bool bTargetIdWasPresent = false;
		if (!TryReadOptionalString(
			*Parameters,
			TargetIdField,
			MaximumIdentifierLength,
			TargetId,
			bTargetIdWasPresent,
			FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}

		if (OutCommand.ActionTargetType == EArenaActionTargetType::None && bTargetIdWasPresent)
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				TEXT("Field 'targetId' is not allowed when 'targetType' is 'none'."),
				OutErrorCode,
				OutMessage);
		}

		if (OutCommand.ActionTargetType != EArenaActionTargetType::None && !bTargetIdWasPresent)
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				TEXT("Field 'targetId' is required for the selected 'targetType'."),
				OutErrorCode,
				OutMessage);
		}

		OutCommand.ActionId = FName(*ActionId);
		OutCommand.TargetId = bTargetIdWasPresent ? FName(*TargetId) : NAME_None;
		return true;
	}

	if (CommandName == TEXT("stop") || CommandName == TEXT("leave"))
	{
		OutCommand.CommandType = CommandName == TEXT("stop")
			? EArenaCommandType::Stop
			: EArenaCommandType::Leave;
		if (!HasOnlyFields(*Parameters, TSet<FString>(), FieldError))
		{
			return SetParseError(
				EArenaCommandError::InvalidRequest,
				FieldError,
				OutErrorCode,
				OutMessage);
		}
		return true;
	}

	return SetParseError(
		EArenaCommandError::UnsupportedCommand,
		FString::Printf(TEXT("Command '%s' is not supported."), *CommandName),
		OutErrorCode,
		OutMessage);
}

FString FArenaCommandJsonProtocol::SerializeCommandState(
	const FArenaCommandStateRecord& StateRecord)
{
	return SerializeResult(
		StateRecord.RequestId,
		StateRecord.Status,
		StateRecord.ErrorCode,
		StateRecord.Message);
}

FString FArenaCommandJsonProtocol::SerializeResult(
	const FString& RequestId,
	const EArenaCommandStatus Status,
	const EArenaCommandError ErrorCode,
	const FString& Message)
{
	using namespace ArenaCommandJsonProtocol;

	const TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(VersionField, ProtocolVersion);
	JsonObject->SetStringField(RequestIdField, RequestId);
	JsonObject->SetStringField(TEXT("status"), StatusToString(Status));
	if (ErrorCode == EArenaCommandError::None)
	{
		JsonObject->SetField(TEXT("errorCode"), MakeShared<FJsonValueNull>());
	}
	else
	{
		JsonObject->SetStringField(TEXT("errorCode"), ErrorToString(ErrorCode));
	}
	JsonObject->SetStringField(TEXT("message"), Message);

	FString JsonMessage;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonMessage);
	FJsonSerializer::Serialize(JsonObject, Writer);
	return JsonMessage;
}
