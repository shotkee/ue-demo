// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaParticipantManager.h"

#include "ArenaMannequinCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogArenaParticipants, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogArenaCommands, Log, All);

AArenaParticipantManager::AArenaParticipantManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

AArenaMannequinCharacter* AArenaParticipantManager::SpawnParticipant(
	const FString& EntityId,
	const FText& DisplayName)
{
	const FString NormalizedEntityId = NormalizeEntityId(EntityId);
	if (NormalizedEntityId.IsEmpty())
	{
		UE_LOG(LogArenaParticipants, Warning, TEXT("Cannot spawn participant: EntityId is empty."));
		return nullptr;
	}

	if (!MannequinClass || !IsValid(SpawnOrigin))
	{
		UE_LOG(LogArenaParticipants, Warning, TEXT("Cannot spawn participant '%s': MannequinClass or SpawnOrigin is not configured."), *NormalizedEntityId);
		return nullptr;
	}

	if (AArenaMannequinCharacter* ExistingParticipant = FindParticipant(NormalizedEntityId))
	{
		UE_LOG(LogArenaParticipants, Warning, TEXT("Cannot spawn participant '%s': EntityId already exists."), *NormalizedEntityId);
		return nullptr;
	}

	if (Participants.Num() >= FMath::Max(1, MaximumParticipants))
	{
		UE_LOG(LogArenaParticipants, Warning, TEXT("Cannot spawn participant '%s': participant limit reached."), *NormalizedEntityId);
		return nullptr;
	}

	FVector SpawnLocation;
	if (!TryFindSpawnLocation(SpawnLocation))
	{
		UE_LOG(LogArenaParticipants, Warning, TEXT("Cannot spawn participant '%s': no free NavMesh location was found."), *NormalizedEntityId);
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;

	AArenaMannequinCharacter* Participant = GetWorld()->SpawnActor<AArenaMannequinCharacter>(
		MannequinClass,
		SpawnLocation,
		SpawnOrigin->GetActorRotation(),
		SpawnParameters);

	if (!IsValid(Participant))
	{
		UE_LOG(LogArenaParticipants, Warning, TEXT("Cannot spawn participant '%s': SpawnActor failed."), *NormalizedEntityId);
		return nullptr;
	}

	if (!Participant->InitializeArenaParticipant(NormalizedEntityId, DisplayName, Participant->GetActorLocation()))
	{
		UE_LOG(LogArenaParticipants, Warning, TEXT("Cannot spawn participant '%s': initialization failed."), *NormalizedEntityId);
		Participant->Destroy();
		return nullptr;
	}

	Participants.Add(NormalizedEntityId, Participant);
	CommandQueues.FindOrAdd(NormalizedEntityId);
	Participant->OnArenaMovementFinished.AddUObject(
		this,
		&AArenaParticipantManager::HandleParticipantMovementFinished);
	Participant->OnDestroyed.AddDynamic(this, &AArenaParticipantManager::HandleParticipantDestroyed);
	return Participant;
}

AArenaMannequinCharacter* AArenaParticipantManager::FindParticipant(const FString& EntityId) const
{
	const TObjectPtr<AArenaMannequinCharacter>* Participant = Participants.Find(NormalizeEntityId(EntityId));
	return Participant != nullptr && IsValid(Participant->Get()) ? Participant->Get() : nullptr;
}

bool AArenaParticipantManager::SetParticipantDisplayName(const FString& EntityId, const FText& NewDisplayName)
{
	AArenaMannequinCharacter* Participant = FindParticipant(EntityId);
	return IsValid(Participant) && Participant->SetArenaDisplayName(NewDisplayName);
}

bool AArenaParticipantManager::RemoveParticipant(const FString& EntityId)
{
	const FString NormalizedEntityId = NormalizeEntityId(EntityId);
	AArenaMannequinCharacter* Participant = FindParticipant(NormalizedEntityId);
	if (!IsValid(Participant))
	{
		return false;
	}

	CancelParticipantCommands(
		NormalizedEntityId,
		EArenaCommandError::ParticipantRemoved,
		TEXT("Participant was removed."));
	return DestroyRegisteredParticipant(NormalizedEntityId, Participant);
}

int32 AArenaParticipantManager::GetParticipantCount() const
{
	return Participants.Num();
}

FArenaCommandResult AArenaParticipantManager::SubmitArenaCommand(const FArenaCommand& InCommand)
{
	FArenaCommand Command = InCommand;
	Command.RequestId = NormalizeRequestId(Command.RequestId);
	Command.ActorId = NormalizeEntityId(Command.ActorId);

	RecordCommandState(Command, EArenaCommandStatus::Received);

	if (Command.RequestId.IsEmpty() || Command.ActorId.IsEmpty())
	{
		return RejectCommand(
			Command,
			EArenaCommandError::InvalidRequest,
			TEXT("RequestId and ActorId are required."));
	}

	if (SeenRequestIds.Contains(Command.RequestId))
	{
		return RejectCommand(
			Command,
			EArenaCommandError::DuplicateRequestId,
			TEXT("RequestId has already been used in this session."));
	}
	SeenRequestIds.Add(Command.RequestId);

	if (Command.Version != SupportedProtocolVersion)
	{
		return RejectCommand(
			Command,
			EArenaCommandError::UnsupportedVersion,
			FString::Printf(
				TEXT("Unsupported command version %d. Expected version %d."),
				Command.Version,
				SupportedProtocolVersion));
	}

	if (Command.CommandType == EArenaCommandType::Spawn)
	{
		return ProcessSpawnCommand(Command);
	}

	AArenaMannequinCharacter* Participant = FindParticipant(Command.ActorId);
	if (!IsValid(Participant))
	{
		return RejectCommand(
			Command,
			EArenaCommandError::UnknownParticipant,
			TEXT("Participant was not found."));
	}

	if (Command.CommandType == EArenaCommandType::Stop)
	{
		return ProcessStopCommand(Command, Participant);
	}

	if (Command.CommandType == EArenaCommandType::MoveToPoint)
	{
		const TObjectPtr<AActor>* Target = NamedPointTargets.Find(Command.TargetId);
		if (Command.TargetId.IsNone() || Target == nullptr || !IsValid(Target->Get()))
		{
			return RejectCommand(
				Command,
				EArenaCommandError::UnknownTarget,
				TEXT("Named point target was not found."));
		}
	}
	else if (Command.CommandType == EArenaCommandType::MoveToActor)
	{
		if (Command.TargetId.IsNone() || !IsValid(FindParticipant(Command.TargetId.ToString())))
		{
			return RejectCommand(
				Command,
				EArenaCommandError::UnknownTarget,
				TEXT("Target participant was not found."));
		}
	}

	FArenaParticipantCommandQueue& Queue = CommandQueues.FindOrAdd(Command.ActorId);
	if (Queue.PendingCommands.Num() >= FMath::Max(1, MaximumQueuedCommandsPerParticipant))
	{
		return RejectCommand(
			Command,
			EArenaCommandError::QueueFull,
			TEXT("Participant command queue is full."));
	}

	Queue.PendingCommands.Add(Command);
	RecordCommandState(Command, EArenaCommandStatus::Accepted);
	TryStartNextCommand(Command.ActorId);

	return MakeCommandResult(
		true,
		EArenaCommandStatus::Accepted,
		EArenaCommandError::None,
		TEXT("Command was accepted."));
}

FArenaCommandResult AArenaParticipantManager::SubmitSpawnCommand(
	const FString& RequestId,
	const FString& EntityId,
	const FText& DisplayName)
{
	FArenaCommand Command;
	Command.Version = SupportedProtocolVersion;
	Command.RequestId = RequestId;
	Command.ActorId = EntityId;
	Command.CommandType = EArenaCommandType::Spawn;
	Command.DisplayName = DisplayName;
	return SubmitArenaCommand(Command);
}

FArenaCommandResult AArenaParticipantManager::SubmitMoveToPointCommand(
	const FString& RequestId,
	const FString& EntityId,
	const FName TargetId,
	const EArenaMovementMode MovementMode)
{
	FArenaCommand Command;
	Command.Version = SupportedProtocolVersion;
	Command.RequestId = RequestId;
	Command.ActorId = EntityId;
	Command.CommandType = EArenaCommandType::MoveToPoint;
	Command.TargetId = TargetId;
	Command.MovementMode = MovementMode;
	return SubmitArenaCommand(Command);
}

FArenaCommandResult AArenaParticipantManager::SubmitStopCommand(
	const FString& RequestId,
	const FString& EntityId)
{
	FArenaCommand Command;
	Command.Version = SupportedProtocolVersion;
	Command.RequestId = RequestId;
	Command.ActorId = EntityId;
	Command.CommandType = EArenaCommandType::Stop;
	return SubmitArenaCommand(Command);
}

FArenaCommandResult AArenaParticipantManager::SubmitLeaveCommand(
	const FString& RequestId,
	const FString& EntityId)
{
	FArenaCommand Command;
	Command.Version = SupportedProtocolVersion;
	Command.RequestId = RequestId;
	Command.ActorId = EntityId;
	Command.CommandType = EArenaCommandType::Leave;
	return SubmitArenaCommand(Command);
}

TArray<FArenaCommandStateRecord> AArenaParticipantManager::GetCommandHistory() const
{
	return CommandHistory;
}

int32 AArenaParticipantManager::GetQueuedCommandCount(const FString& EntityId) const
{
	const FArenaParticipantCommandQueue* Queue = CommandQueues.Find(NormalizeEntityId(EntityId));
	return Queue != nullptr ? Queue->PendingCommands.Num() : 0;
}

bool AArenaParticipantManager::IsParticipantExecutingCommand(const FString& EntityId) const
{
	const FArenaParticipantCommandQueue* Queue = CommandQueues.Find(NormalizeEntityId(EntityId));
	return Queue != nullptr && Queue->bHasActiveCommand;
}

int32 AArenaParticipantManager::GetSupportedProtocolVersion() const
{
	return SupportedProtocolVersion;
}

FArenaCommandResult AArenaParticipantManager::RejectCommand(
	const FArenaCommand& Command,
	const EArenaCommandError ErrorCode,
	const FString& Message)
{
	RecordCommandState(Command, EArenaCommandStatus::Rejected, ErrorCode, Message);
	return MakeCommandResult(false, EArenaCommandStatus::Rejected, ErrorCode, Message);
}

FArenaCommandResult AArenaParticipantManager::ProcessSpawnCommand(const FArenaCommand& Command)
{
	if (IsValid(FindParticipant(Command.ActorId)))
	{
		return RejectCommand(
			Command,
			EArenaCommandError::DuplicateParticipant,
			TEXT("Participant already exists."));
	}

	RecordCommandState(Command, EArenaCommandStatus::Accepted);
	RecordCommandState(Command, EArenaCommandStatus::Started);

	AArenaMannequinCharacter* Participant = SpawnParticipant(Command.ActorId, Command.DisplayName);
	if (!IsValid(Participant))
	{
		const FString Message = TEXT("Participant could not be spawned.");
		RecordCommandState(
			Command,
			EArenaCommandStatus::Failed,
			EArenaCommandError::ExecutionFailed,
			Message);
		return MakeCommandResult(
			true,
			EArenaCommandStatus::Failed,
			EArenaCommandError::ExecutionFailed,
			Message);
	}

	RecordCommandState(Command, EArenaCommandStatus::Completed);
	return MakeCommandResult(
		true,
		EArenaCommandStatus::Completed,
		EArenaCommandError::None,
		TEXT("Participant was spawned."));
}

FArenaCommandResult AArenaParticipantManager::ProcessStopCommand(
	const FArenaCommand& Command,
	AArenaMannequinCharacter* Participant)
{
	RecordCommandState(Command, EArenaCommandStatus::Accepted);
	CancelParticipantCommands(
		Command.ActorId,
		EArenaCommandError::CancelledByStop,
		TEXT("Command was cancelled by a priority stop command."));
	RecordCommandState(Command, EArenaCommandStatus::Started);
	Participant->StopArenaMovement();
	RecordCommandState(Command, EArenaCommandStatus::Completed);

	return MakeCommandResult(
		true,
		EArenaCommandStatus::Completed,
		EArenaCommandError::None,
		TEXT("Participant was stopped and its queue was cleared."));
}

void AArenaParticipantManager::TryStartNextCommand(const FString& EntityId)
{
	FArenaParticipantCommandQueue* Queue = CommandQueues.Find(EntityId);
	if (Queue == nullptr || Queue->bHasActiveCommand || Queue->PendingCommands.IsEmpty())
	{
		return;
	}

	AArenaMannequinCharacter* Participant = FindParticipant(EntityId);
	if (!IsValid(Participant))
	{
		while (!Queue->PendingCommands.IsEmpty())
		{
			const FArenaCommand Command = Queue->PendingCommands[0];
			Queue->PendingCommands.RemoveAt(0);
			RecordCommandState(
				Command,
				EArenaCommandStatus::Failed,
				EArenaCommandError::UnknownParticipant,
				TEXT("Participant disappeared before command execution."));
		}
		return;
	}

	Queue->ActiveCommand = Queue->PendingCommands[0];
	Queue->PendingCommands.RemoveAt(0);
	Queue->bHasActiveCommand = true;
	RecordCommandState(Queue->ActiveCommand, EArenaCommandStatus::Started);
	ExecuteActiveCommand(EntityId, Participant);
}

void AArenaParticipantManager::ExecuteActiveCommand(
	const FString& EntityId,
	AArenaMannequinCharacter* Participant)
{
	FArenaParticipantCommandQueue* Queue = CommandQueues.Find(EntityId);
	if (Queue == nullptr || !Queue->bHasActiveCommand || !IsValid(Participant))
	{
		return;
	}

	const FArenaCommand Command = Queue->ActiveCommand;
	switch (Command.CommandType)
	{
	case EArenaCommandType::MoveToPoint:
	{
		const TObjectPtr<AActor>* Target = NamedPointTargets.Find(Command.TargetId);
		if (Target == nullptr || !IsValid(Target->Get()))
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::UnknownTarget,
				TEXT("Named point target disappeared before execution."));
			return;
		}

		if (!Participant->MoveToArenaLocation(Target->Get()->GetActorLocation(), Command.MovementMode))
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::UnreachableTarget,
				TEXT("No path to the named point target was found."));
			return;
		}

		return;
	}

	case EArenaCommandType::Leave:
	{
		Queue->bHasActiveCommand = false;
		Queue->ActiveCommand = FArenaCommand();
		RecordCommandState(Command, EArenaCommandStatus::Completed);
		CancelParticipantCommands(
			EntityId,
			EArenaCommandError::ParticipantRemoved,
			TEXT("Command was cancelled because the participant left the arena."));
		DestroyRegisteredParticipant(EntityId, Participant);
		return;
	}

	case EArenaCommandType::MoveToActor:
	case EArenaCommandType::PlayAction:
	case EArenaCommandType::ApproachObject:
	case EArenaCommandType::Spawn:
	case EArenaCommandType::Stop:
	default:
		FinishActiveCommand(
			EntityId,
			EArenaCommandStatus::Failed,
			EArenaCommandError::UnsupportedCommand,
			TEXT("This command is reserved for a later roadmap stage."));
		return;
	}
}

void AArenaParticipantManager::FinishActiveCommand(
	const FString& EntityId,
	const EArenaCommandStatus Status,
	const EArenaCommandError ErrorCode,
	const FString& Message)
{
	FArenaParticipantCommandQueue* Queue = CommandQueues.Find(EntityId);
	if (Queue == nullptr || !Queue->bHasActiveCommand)
	{
		return;
	}

	const FArenaCommand FinishedCommand = Queue->ActiveCommand;
	Queue->ActiveCommand = FArenaCommand();
	Queue->bHasActiveCommand = false;
	RecordCommandState(FinishedCommand, Status, ErrorCode, Message);
	TryStartNextCommand(EntityId);
}

void AArenaParticipantManager::CancelParticipantCommands(
	const FString& EntityId,
	const EArenaCommandError ErrorCode,
	const FString& Message)
{
	FArenaParticipantCommandQueue* Queue = CommandQueues.Find(EntityId);
	if (Queue == nullptr)
	{
		return;
	}

	if (Queue->bHasActiveCommand)
	{
		const FArenaCommand ActiveCommand = Queue->ActiveCommand;
		Queue->ActiveCommand = FArenaCommand();
		Queue->bHasActiveCommand = false;
		RecordCommandState(ActiveCommand, EArenaCommandStatus::Cancelled, ErrorCode, Message);
	}

	for (const FArenaCommand& PendingCommand : Queue->PendingCommands)
	{
		RecordCommandState(PendingCommand, EArenaCommandStatus::Cancelled, ErrorCode, Message);
	}
	Queue->PendingCommands.Reset();
}

bool AArenaParticipantManager::DestroyRegisteredParticipant(
	const FString& EntityId,
	AArenaMannequinCharacter* Participant)
{
	if (!IsValid(Participant))
	{
		return false;
	}

	Participant->OnDestroyed.RemoveDynamic(this, &AArenaParticipantManager::HandleParticipantDestroyed);
	Participant->OnArenaMovementFinished.RemoveAll(this);
	Participants.Remove(EntityId);
	CommandQueues.Remove(EntityId);
	Participant->StopArenaMovement();
	return Participant->Destroy();
}

void AArenaParticipantManager::RecordCommandState(
	const FArenaCommand& Command,
	const EArenaCommandStatus Status,
	const EArenaCommandError ErrorCode,
	const FString& Message)
{
	FArenaCommandStateRecord StateRecord;
	StateRecord.Version = Command.Version;
	StateRecord.RequestId = Command.RequestId;
	StateRecord.ActorId = Command.ActorId;
	StateRecord.CommandType = Command.CommandType;
	StateRecord.Status = Status;
	StateRecord.ErrorCode = ErrorCode;
	StateRecord.Message = Message;
	StateRecord.TimestampUtc = FDateTime::UtcNow();

	const int32 MaximumEntries = FMath::Max(1, MaximumCommandHistoryEntries);
	if (CommandHistory.Num() >= MaximumEntries)
	{
		CommandHistory.RemoveAt(0, CommandHistory.Num() - MaximumEntries + 1);
	}
	CommandHistory.Add(StateRecord);
	OnCommandStatusChanged.Broadcast(StateRecord);

	const UEnum* CommandTypeEnum = StaticEnum<EArenaCommandType>();
	const UEnum* StatusEnum = StaticEnum<EArenaCommandStatus>();
	const UEnum* ErrorEnum = StaticEnum<EArenaCommandError>();
	const FString CommandTypeName = CommandTypeEnum->GetNameStringByValue(static_cast<int64>(Command.CommandType));
	const FString StatusName = StatusEnum->GetNameStringByValue(static_cast<int64>(Status));
	const FString ErrorName = ErrorEnum->GetNameStringByValue(static_cast<int64>(ErrorCode));

	if (Status == EArenaCommandStatus::Rejected || Status == EArenaCommandStatus::Failed)
	{
		UE_LOG(
			LogArenaCommands,
			Warning,
			TEXT("Request='%s' Actor='%s' Command=%s Status=%s Error=%s Message='%s'"),
			*Command.RequestId,
			*Command.ActorId,
			*CommandTypeName,
			*StatusName,
			*ErrorName,
			*Message);
	}
	else
	{
		UE_LOG(
			LogArenaCommands,
			Log,
			TEXT("Request='%s' Actor='%s' Command=%s Status=%s Error=%s Message='%s'"),
			*Command.RequestId,
			*Command.ActorId,
			*CommandTypeName,
			*StatusName,
			*ErrorName,
			*Message);
	}
}

FArenaCommandResult AArenaParticipantManager::MakeCommandResult(
	const bool bAccepted,
	const EArenaCommandStatus Status,
	const EArenaCommandError ErrorCode,
	const FString& Message)
{
	FArenaCommandResult Result;
	Result.bAccepted = bAccepted;
	Result.Status = Status;
	Result.ErrorCode = ErrorCode;
	Result.Message = Message;
	return Result;
}

bool AArenaParticipantManager::TryFindSpawnLocation(FVector& OutSpawnLocation) const
{
	if (!MannequinClass || !IsValid(SpawnOrigin) || !IsValid(GetWorld()))
	{
		return false;
	}

	const AArenaMannequinCharacter* DefaultMannequin = MannequinClass->GetDefaultObject<AArenaMannequinCharacter>();
	const UCapsuleComponent* Capsule = IsValid(DefaultMannequin) ? DefaultMannequin->GetCapsuleComponent() : nullptr;
	if (!IsValid(Capsule))
	{
		return false;
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!IsValid(NavigationSystem))
	{
		return false;
	}

	const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
	const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const int32 AttemptCount = FMath::Max(1, MaximumSpawnAttempts);

	for (int32 Attempt = 0; Attempt < AttemptCount; ++Attempt)
	{
		FNavLocation NavLocation;
		if (!NavigationSystem->GetRandomReachablePointInRadius(
			SpawnOrigin->GetActorLocation(),
			FMath::Max(0.0f, SpawnRadius),
			NavLocation))
		{
			continue;
		}

		const FVector CandidateLocation = NavLocation.Location + FVector(0.0f, 0.0f, CapsuleHalfHeight + 2.0f);
		if (IsSpawnLocationFree(CandidateLocation, CapsuleRadius, CapsuleHalfHeight))
		{
			OutSpawnLocation = CandidateLocation;
			return true;
		}
	}

	return false;
}

bool AArenaParticipantManager::IsSpawnLocationFree(
	const FVector& CandidateLocation,
	const float CapsuleRadius,
	const float CapsuleHalfHeight) const
{
	const float MinimumDistanceSquared = FMath::Square(FMath::Max(0.0f, MinimumParticipantDistance));
	for (const TPair<FString, TObjectPtr<AArenaMannequinCharacter>>& Entry : Participants)
	{
		const AArenaMannequinCharacter* Participant = Entry.Value.Get();
		if (IsValid(Participant)
			&& FVector::DistSquared2D(CandidateLocation, Participant->GetActorLocation()) < MinimumDistanceSquared)
		{
			return false;
		}
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ArenaParticipantSpawn), false, this);
	const FCollisionShape SpawnShape = FCollisionShape::MakeCapsule(
		CapsuleRadius + FMath::Max(0.0f, SpawnClearance),
		CapsuleHalfHeight);

	return !GetWorld()->OverlapBlockingTestByChannel(
		CandidateLocation,
		FQuat::Identity,
		ECC_Pawn,
		SpawnShape,
		QueryParams);
}

FString AArenaParticipantManager::NormalizeEntityId(const FString& EntityId)
{
	FString NormalizedEntityId = EntityId;
	NormalizedEntityId.TrimStartAndEndInline();
	return NormalizedEntityId;
}

FString AArenaParticipantManager::NormalizeRequestId(const FString& RequestId)
{
	FString NormalizedRequestId = RequestId;
	NormalizedRequestId.TrimStartAndEndInline();
	return NormalizedRequestId;
}

void AArenaParticipantManager::HandleParticipantMovementFinished(
	AArenaMannequinCharacter* Participant,
	const bool bSucceeded)
{
	if (!IsValid(Participant))
	{
		return;
	}

	const FString EntityId = Participant->GetEntityId();
	const FArenaParticipantCommandQueue* Queue = CommandQueues.Find(EntityId);
	if (Queue == nullptr
		|| !Queue->bHasActiveCommand
		|| Queue->ActiveCommand.CommandType != EArenaCommandType::MoveToPoint)
	{
		return;
	}

	if (bSucceeded)
	{
		FinishActiveCommand(
			EntityId,
			EArenaCommandStatus::Completed,
			EArenaCommandError::None,
			TEXT("Participant reached the named point target."));
	}
	else
	{
		FinishActiveCommand(
			EntityId,
			EArenaCommandStatus::Failed,
			EArenaCommandError::UnreachableTarget,
			TEXT("Participant could not reach the named point target."));
	}
}

void AArenaParticipantManager::HandleParticipantDestroyed(AActor* DestroyedActor)
{
	FString DestroyedEntityId;
	for (const TPair<FString, TObjectPtr<AArenaMannequinCharacter>>& Entry : Participants)
	{
		if (Entry.Value.Get() == DestroyedActor)
		{
			DestroyedEntityId = Entry.Key;
			break;
		}
	}

	if (DestroyedEntityId.IsEmpty())
	{
		return;
	}

	CancelParticipantCommands(
		DestroyedEntityId,
		EArenaCommandError::ParticipantRemoved,
		TEXT("Command was cancelled because the participant was destroyed."));
	CommandQueues.Remove(DestroyedEntityId);
	Participants.Remove(DestroyedEntityId);
}
