// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ArenaCommandTypes.h"
#include "CoreMinimal.h"

class DEMO_API FArenaCommandJsonProtocol
{
public:
	static constexpr int32 ProtocolVersion = 1;
	static constexpr int32 MaximumMessageLength = 16384;
	static constexpr int32 MaximumIdentifierLength = 128;
	static constexpr int32 MaximumDisplayNameLength = 64;

	static bool TryParseCommand(
		const FString& JsonMessage,
		FArenaCommand& OutCommand,
		EArenaCommandError& OutErrorCode,
		FString& OutMessage);

	static FString SerializeCommandState(const FArenaCommandStateRecord& StateRecord);

	static FString SerializeResult(
		const FString& RequestId,
		EArenaCommandStatus Status,
		EArenaCommandError ErrorCode,
		const FString& Message);
};
