// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArenaCommandTypes.generated.h"

UENUM(BlueprintType)
enum class EArenaMovementMode : uint8
{
	Walk UMETA(DisplayName = "Walk"),
	Run UMETA(DisplayName = "Run")
};

UENUM(BlueprintType)
enum class EArenaCommandType : uint8
{
	Spawn UMETA(DisplayName = "Spawn"),
	MoveToPoint UMETA(DisplayName = "Move To Point"),
	MoveToActor UMETA(DisplayName = "Move To Actor"),
	PlayAction UMETA(DisplayName = "Play Action"),
	ApproachObject UMETA(DisplayName = "Approach Object"),
	Stop UMETA(DisplayName = "Stop"),
	Leave UMETA(DisplayName = "Leave")
};

UENUM(BlueprintType)
enum class EArenaCommandStatus : uint8
{
	Received UMETA(DisplayName = "Received"),
	Accepted UMETA(DisplayName = "Accepted"),
	Started UMETA(DisplayName = "Started"),
	Completed UMETA(DisplayName = "Completed"),
	Rejected UMETA(DisplayName = "Rejected"),
	Failed UMETA(DisplayName = "Failed"),
	Cancelled UMETA(DisplayName = "Cancelled")
};

UENUM(BlueprintType)
enum class EArenaCommandError : uint8
{
	None UMETA(DisplayName = "None"),
	InvalidRequest UMETA(DisplayName = "Invalid Request"),
	UnsupportedVersion UMETA(DisplayName = "Unsupported Version"),
	DuplicateRequestId UMETA(DisplayName = "Duplicate Request ID"),
	DuplicateParticipant UMETA(DisplayName = "Duplicate Participant"),
	UnknownParticipant UMETA(DisplayName = "Unknown Participant"),
	UnknownTarget UMETA(DisplayName = "Unknown Target"),
	UnreachableTarget UMETA(DisplayName = "Unreachable Target"),
	QueueFull UMETA(DisplayName = "Queue Full"),
	UnsupportedCommand UMETA(DisplayName = "Unsupported Command"),
	ExecutionFailed UMETA(DisplayName = "Execution Failed"),
	CancelledByStop UMETA(DisplayName = "Cancelled By Stop"),
	ParticipantRemoved UMETA(DisplayName = "Participant Removed")
};

USTRUCT(BlueprintType)
struct DEMO_API FArenaCommand
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Command")
	int32 Version = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Command")
	FString RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Command")
	FString ActorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Command")
	EArenaCommandType CommandType = EArenaCommandType::MoveToPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Command")
	FName TargetId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Command")
	FName InteractionPointId = FName(TEXT("default"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Command")
	FName ActionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Command")
	EArenaMovementMode MovementMode = EArenaMovementMode::Walk;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Command")
	FText DisplayName;
};

USTRUCT(BlueprintType)
struct DEMO_API FArenaCommandResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	bool bAccepted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	EArenaCommandStatus Status = EArenaCommandStatus::Rejected;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	EArenaCommandError ErrorCode = EArenaCommandError::None;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	FString Message;
};

USTRUCT(BlueprintType)
struct DEMO_API FArenaCommandStateRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	int32 Version = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	FString RequestId;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	FString ActorId;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	EArenaCommandType CommandType = EArenaCommandType::MoveToPoint;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	EArenaCommandStatus Status = EArenaCommandStatus::Received;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	EArenaCommandError ErrorCode = EArenaCommandError::None;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "Arena|Command")
	FDateTime TimestampUtc;
};
