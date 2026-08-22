// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaParticipantManager.h"

#include "ArenaCommandJsonProtocol.h"
#include "ArenaCommandPanelWidget.h"
#include "ArenaInteractable.h"
#include "ArenaMannequinCharacter.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogArenaParticipants, Log, All);
DEFINE_LOG_CATEGORY_STATIC(LogArenaCommands, Log, All);

AArenaParticipantManager::AArenaParticipantManager()
{
	PrimaryActorTick.bCanEverTick = false;

	FArenaActionDefinition PunchAction;
	static ConstructorHelpers::FObjectFinder<UAnimMontage> PunchMontage(
		TEXT("/Game/Arena/Animations/AM_ArenaAttack.AM_ArenaAttack"));
	if (PunchMontage.Succeeded())
	{
		PunchAction.Montage = PunchMontage.Object;
	}
	PunchAction.PlayRate = 1.0f;
	PunchAction.bStopMovementBeforeAction = true;
	PunchAction.bRequiresTarget = true;
	ActionRegistry.Add(FName(TEXT("punch")), PunchAction);
}

void AArenaParticipantManager::BeginPlay()
{
	Super::BeginPlay();
	RefreshArenaObjects();
	CreateLocalCommandPanel();
}

void AArenaParticipantManager::CreateLocalCommandPanel()
{
	if (!bShowLocalCommandPanel || GetNetMode() == NM_DedicatedServer || !IsValid(GetWorld()))
	{
		return;
	}

	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	if (!IsValid(PlayerController) || !PlayerController->IsLocalController())
	{
		return;
	}

	LocalCommandPanel = CreateWidget<UArenaCommandPanelWidget>(
		PlayerController,
		UArenaCommandPanelWidget::StaticClass());
	if (!IsValid(LocalCommandPanel))
	{
		UE_LOG(LogArenaCommands, Warning, TEXT("Failed to create the local arena command panel."));
		return;
	}

	LocalCommandPanel->InitializeWithManager(this);
	LocalCommandPanel->AddToViewport(100);

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
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
	Participant->OnArenaActionFinished.AddUObject(
		this,
		&AArenaParticipantManager::HandleParticipantActionFinished);
	Participant->OnArenaActionEvent.AddUObject(
		this,
		&AArenaParticipantManager::HandleParticipantActionEvent);
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

TArray<FString> AArenaParticipantManager::GetParticipantIds() const
{
	TArray<FString> ParticipantIds;
	Participants.GetKeys(ParticipantIds);
	ParticipantIds.Sort();
	return ParticipantIds;
}

FArenaCommandResult AArenaParticipantManager::SubmitArenaCommand(const FArenaCommand& InCommand)
{
	FArenaCommand Command = InCommand;
	Command.RequestId = NormalizeRequestId(Command.RequestId);
	Command.ActorId = NormalizeEntityId(Command.ActorId);
	Command.TargetId = NormalizeNameId(Command.TargetId);
	Command.InteractionPointId = NormalizeNameId(Command.InteractionPointId);
	Command.ActionId = NormalizeNameId(Command.ActionId);
	if (Command.CommandType == EArenaCommandType::ApproachObject && Command.InteractionPointId.IsNone())
	{
		Command.InteractionPointId = FName(TEXT("default"));
	}

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
		AArenaMannequinCharacter* TargetParticipant = FindParticipant(Command.TargetId.ToString());
		if (Command.TargetId.IsNone() || !IsValid(TargetParticipant))
		{
			return RejectCommand(
				Command,
				EArenaCommandError::UnknownTarget,
				TEXT("Target participant was not found."));
		}

		if (TargetParticipant == Participant)
		{
			return RejectCommand(
				Command,
				EArenaCommandError::InvalidRequest,
				TEXT("A participant cannot move to itself."));
		}
	}
	else if (Command.CommandType == EArenaCommandType::ApproachObject)
	{
		AActor* TargetObject = nullptr;
		FVector InteractionPoint;
		if (!TryResolveArenaObjectInteractionPoint(
			Command.TargetId,
			Command.InteractionPointId,
			TargetObject,
			InteractionPoint))
		{
			return RejectCommand(
				Command,
				EArenaCommandError::UnknownTarget,
				TEXT("Arena object or interaction point was not found."));
		}
	}
	else if (Command.CommandType == EArenaCommandType::PlayAction)
	{
		const FArenaActionDefinition* ActionDefinition = FindActionDefinition(Command.ActionId);
		if (ActionDefinition == nullptr)
		{
			return RejectCommand(
				Command,
				EArenaCommandError::UnknownAction,
				TEXT("ActionId is not registered."));
		}

		if (!IsValid(ActionDefinition->Montage) || ActionDefinition->PlayRate <= 0.0f)
		{
			return RejectCommand(
				Command,
				EArenaCommandError::ActionUnavailable,
				TEXT("The registered action has no playable montage."));
		}

		AActor* ActionTarget = nullptr;
		EArenaCommandError TargetError = EArenaCommandError::None;
		FString TargetMessage;
		if (!TryResolveActionTarget(
			Command,
			*ActionDefinition,
			ActionTarget,
			TargetError,
			TargetMessage))
		{
			return RejectCommand(Command, TargetError, TargetMessage);
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

FString AArenaParticipantManager::SubmitArenaJsonCommand(const FString& JsonMessage)
{
	FArenaCommand Command;
	EArenaCommandError ParseError = EArenaCommandError::None;
	FString ParseMessage;
	if (!FArenaCommandJsonProtocol::TryParseCommand(
		JsonMessage,
		Command,
		ParseError,
		ParseMessage))
	{
		if (!Command.RequestId.IsEmpty())
		{
			RecordCommandState(
				Command,
				EArenaCommandStatus::Rejected,
				ParseError,
				ParseMessage);
		}

		return FArenaCommandJsonProtocol::SerializeResult(
			Command.RequestId,
			EArenaCommandStatus::Rejected,
			ParseError,
			ParseMessage);
	}

	const FArenaCommandResult Result = SubmitArenaCommand(Command);
	return FArenaCommandJsonProtocol::SerializeResult(
		Command.RequestId,
		Result.Status,
		Result.ErrorCode,
		Result.Message);
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

FArenaCommandResult AArenaParticipantManager::SubmitMoveToActorCommand(
	const FString& RequestId,
	const FString& EntityId,
	const FString& TargetEntityId,
	const EArenaMovementMode MovementMode)
{
	FArenaCommand Command;
	Command.Version = SupportedProtocolVersion;
	Command.RequestId = RequestId;
	Command.ActorId = EntityId;
	Command.CommandType = EArenaCommandType::MoveToActor;
	Command.TargetId = FName(*NormalizeEntityId(TargetEntityId));
	Command.MovementMode = MovementMode;
	return SubmitArenaCommand(Command);
}

FArenaCommandResult AArenaParticipantManager::SubmitApproachObjectCommand(
	const FString& RequestId,
	const FString& EntityId,
	const FName ObjectId,
	const FName InteractionPointId,
	const EArenaMovementMode MovementMode)
{
	FArenaCommand Command;
	Command.Version = SupportedProtocolVersion;
	Command.RequestId = RequestId;
	Command.ActorId = EntityId;
	Command.CommandType = EArenaCommandType::ApproachObject;
	Command.TargetId = ObjectId;
	Command.InteractionPointId = InteractionPointId;
	Command.MovementMode = MovementMode;
	return SubmitArenaCommand(Command);
}

FArenaCommandResult AArenaParticipantManager::SubmitPlayActionCommand(
	const FString& RequestId,
	const FString& EntityId,
	const FName ActionId,
	const EArenaActionTargetType TargetType,
	const FName TargetId)
{
	FArenaCommand Command;
	Command.Version = SupportedProtocolVersion;
	Command.RequestId = RequestId;
	Command.ActorId = EntityId;
	Command.CommandType = EArenaCommandType::PlayAction;
	Command.ActionId = ActionId;
	Command.ActionTargetType = TargetType;
	Command.TargetId = TargetId;
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

int32 AArenaParticipantManager::RefreshArenaObjects()
{
	for (const TPair<FName, TObjectPtr<AActor>>& Entry : ArenaObjects)
	{
		if (IsValid(Entry.Value))
		{
			Entry.Value->OnDestroyed.RemoveDynamic(this, &AArenaParticipantManager::HandleArenaObjectDestroyed);
		}
	}
	ArenaObjects.Reset();

	TArray<AActor*> FoundObjects;
	UGameplayStatics::GetAllActorsWithInterface(this, UArenaInteractable::StaticClass(), FoundObjects);
	for (AActor* Object : FoundObjects)
	{
		if (!IsValid(Object))
		{
			continue;
		}

		const FName ObjectId = NormalizeNameId(IArenaInteractable::Execute_GetArenaObjectId(Object));
		if (ObjectId.IsNone())
		{
			UE_LOG(LogArenaCommands, Warning, TEXT("Ignoring arena object '%s': ObjectId is empty."), *Object->GetName());
			continue;
		}

		if (ArenaObjects.Contains(ObjectId))
		{
			UE_LOG(LogArenaCommands, Warning, TEXT("Ignoring arena object '%s': ObjectId '%s' is duplicated."), *Object->GetName(), *ObjectId.ToString());
			continue;
		}

		ArenaObjects.Add(ObjectId, Object);
		Object->OnDestroyed.AddUniqueDynamic(this, &AArenaParticipantManager::HandleArenaObjectDestroyed);
	}

	UE_LOG(LogArenaCommands, Log, TEXT("Registered %d arena object(s)."), ArenaObjects.Num());
	return ArenaObjects.Num();
}

AActor* AArenaParticipantManager::FindArenaObject(const FName ObjectId) const
{
	const TObjectPtr<AActor>* Object = ArenaObjects.Find(NormalizeNameId(ObjectId));
	return Object != nullptr && IsValid(Object->Get()) ? Object->Get() : nullptr;
}

int32 AArenaParticipantManager::GetArenaObjectCount() const
{
	return ArenaObjects.Num();
}

TArray<FName> AArenaParticipantManager::GetArenaObjectIds() const
{
	TArray<FName> ObjectIds;
	ArenaObjects.GetKeys(ObjectIds);
	ObjectIds.Sort([](const FName& Left, const FName& Right)
	{
		return Left.LexicalLess(Right);
	});
	return ObjectIds;
}

TArray<FName> AArenaParticipantManager::GetNamedPointIds() const
{
	TArray<FName> PointIds;
	NamedPointTargets.GetKeys(PointIds);
	PointIds.Sort([](const FName& Left, const FName& Right)
	{
		return Left.LexicalLess(Right);
	});
	return PointIds;
}

TArray<FName> AArenaParticipantManager::GetRegisteredActionIds() const
{
	TArray<FName> ActionIds;
	ActionRegistry.GetKeys(ActionIds);
	ActionIds.Sort(FNameLexicalLess());
	return ActionIds;
}

bool AArenaParticipantManager::IsActionRegistered(const FName ActionId) const
{
	return FindActionDefinition(ActionId) != nullptr;
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

	Participant->SetArenaDisplayNameColor(Command.DisplayNameColor);

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
	Participant->StopArenaActionMontage();
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

	case EArenaCommandType::MoveToActor:
	{
		AArenaMannequinCharacter* TargetParticipant = FindParticipant(Command.TargetId.ToString());
		if (!IsValid(TargetParticipant) || TargetParticipant == Participant)
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::UnknownTarget,
				TEXT("Target participant disappeared before execution."));
			return;
		}

		if (!Participant->MoveToArenaActor(TargetParticipant, Command.MovementMode))
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::UnreachableTarget,
				TEXT("No path to the target participant was found."));
			return;
		}

		return;
	}

	case EArenaCommandType::ApproachObject:
	{
		AActor* TargetObject = nullptr;
		FVector InteractionPoint;
		if (!TryResolveArenaObjectInteractionPoint(
			Command.TargetId,
			Command.InteractionPointId,
			TargetObject,
			InteractionPoint))
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::UnknownTarget,
				TEXT("Arena object or interaction point disappeared before execution."));
			return;
		}

		FVector NavigationLocation;
		if (!TryProjectInteractionPointToNavigation(InteractionPoint, NavigationLocation)
			|| !Participant->MoveToArenaLocation(NavigationLocation, Command.MovementMode))
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::UnreachableTarget,
				TEXT("No path to the object interaction point was found."));
			return;
		}

		return;
	}

	case EArenaCommandType::PlayAction:
	{
		const FArenaActionDefinition* ActionDefinition = FindActionDefinition(Command.ActionId);
		if (ActionDefinition == nullptr || !IsValid(ActionDefinition->Montage))
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::ActionUnavailable,
				TEXT("The action disappeared from the registry before execution."));
			return;
		}

		AActor* ActionTarget = nullptr;
		EArenaCommandError TargetError = EArenaCommandError::None;
		FString TargetMessage;
		if (!TryResolveActionTarget(
			Command,
			*ActionDefinition,
			ActionTarget,
			TargetError,
			TargetMessage))
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				TargetError,
				TargetMessage);
			return;
		}

		if (IsValid(ActionTarget))
		{
			Participant->FaceArenaTarget(ActionTarget);
		}

		if (Participant->StartArenaActionMontage(
			ActionDefinition->Montage,
			ActionDefinition->PlayRate,
			ActionDefinition->bStopMovementBeforeAction) <= 0.0f)
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::ActionUnavailable,
				TEXT("The action montage could not be started."));
			return;
		}

		return;
	}

	case EArenaCommandType::Leave:
	{
		Queue->bHasActiveCommand = false;
		Queue->ActiveCommand = FArenaCommand();
		CancelParticipantCommands(
			EntityId,
			EArenaCommandError::ParticipantRemoved,
			TEXT("Command was cancelled because the participant left the arena."));
		const bool bParticipantRemoved = DestroyRegisteredParticipant(EntityId, Participant);
		RecordCommandState(
			Command,
			bParticipantRemoved ? EArenaCommandStatus::Completed : EArenaCommandStatus::Failed,
			bParticipantRemoved ? EArenaCommandError::None : EArenaCommandError::ExecutionFailed,
			bParticipantRemoved
				? TEXT("Participant left the arena.")
				: TEXT("Participant could not be removed from the arena."));
		return;
	}

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

void AArenaParticipantManager::AbortCommandsTargetingParticipant(
	const FString& RemovedEntityId)
{
	TArray<FString> ParticipantIds;
	Participants.GetKeys(ParticipantIds);
	for (const FString& ParticipantId : ParticipantIds)
	{
		FArenaParticipantCommandQueue* Queue = CommandQueues.Find(ParticipantId);
		if (Queue == nullptr
			|| !Queue->bHasActiveCommand
			|| Queue->ActiveCommand.CommandType != EArenaCommandType::MoveToActor
			|| NormalizeEntityId(Queue->ActiveCommand.TargetId.ToString()) != RemovedEntityId)
		{
			continue;
		}

		AArenaMannequinCharacter* Participant = FindParticipant(ParticipantId);
		const FArenaCommand FailedCommand = Queue->ActiveCommand;
		TArray<FArenaCommand> CancelledCommands = MoveTemp(Queue->PendingCommands);
		Queue->ActiveCommand = FArenaCommand();
		Queue->bHasActiveCommand = false;
		Queue->PendingCommands.Reset();

		RecordCommandState(
			FailedCommand,
			EArenaCommandStatus::Failed,
			EArenaCommandError::UnknownTarget,
			TEXT("Target participant was removed during movement."));
		for (const FArenaCommand& CancelledCommand : CancelledCommands)
		{
			RecordCommandState(
				CancelledCommand,
				EArenaCommandStatus::Cancelled,
				EArenaCommandError::UnknownTarget,
				TEXT("Command was cancelled because the active movement target was removed."));
		}

		if (IsValid(Participant))
		{
			Participant->StopArenaMovement();
		}

		UE_LOG(
			LogArenaCommands,
			Warning,
			TEXT("Participant '%s' stopped because target '%s' was removed; cancelled %d pending command(s)."),
			*ParticipantId,
			*RemovedEntityId,
			CancelledCommands.Num());
	}
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
	Participant->OnArenaActionFinished.RemoveAll(this);
	Participant->OnArenaActionEvent.RemoveAll(this);
	Participants.Remove(EntityId);
	AbortCommandsTargetingParticipant(EntityId);
	CommandQueues.Remove(EntityId);
	Participant->StopArenaMovement();
	Participant->StopArenaActionMontage();
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
	OnJsonCommandResponse.Broadcast(
		FArenaCommandJsonProtocol::SerializeCommandState(StateRecord));

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

FName AArenaParticipantManager::NormalizeNameId(const FName NameId)
{
	if (NameId.IsNone())
	{
		return NAME_None;
	}

	FString NormalizedName = NameId.ToString();
	NormalizedName.TrimStartAndEndInline();
	return NormalizedName.IsEmpty() ? NAME_None : FName(*NormalizedName);
}

bool AArenaParticipantManager::TryResolveArenaObjectInteractionPoint(
	const FName ObjectId,
	const FName InteractionPointId,
	AActor*& OutObject,
	FVector& OutWorldLocation) const
{
	OutObject = FindArenaObject(ObjectId);
	if (!IsValid(OutObject)
		|| !OutObject->GetClass()->ImplementsInterface(UArenaInteractable::StaticClass()))
	{
		return false;
	}

	return IArenaInteractable::Execute_GetArenaInteractionPoint(
		OutObject,
		NormalizeNameId(InteractionPointId),
		OutWorldLocation);
}

bool AArenaParticipantManager::TryProjectInteractionPointToNavigation(
	const FVector& InteractionPoint,
	FVector& OutNavigationLocation) const
{
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!IsValid(NavigationSystem))
	{
		return false;
	}

	FNavLocation ProjectedLocation;
	if (!NavigationSystem->ProjectPointToNavigation(
		InteractionPoint,
		ProjectedLocation,
		InteractionPointProjectionExtent.GetAbs()))
	{
		return false;
	}

	OutNavigationLocation = ProjectedLocation.Location;
	return true;
}

const FArenaActionDefinition* AArenaParticipantManager::FindActionDefinition(const FName ActionId) const
{
	return ActionRegistry.Find(NormalizeNameId(ActionId));
}

bool AArenaParticipantManager::TryResolveActionTarget(
	const FArenaCommand& Command,
	const FArenaActionDefinition& ActionDefinition,
	AActor*& OutTarget,
	EArenaCommandError& OutError,
	FString& OutMessage) const
{
	OutTarget = nullptr;
	OutError = EArenaCommandError::None;
	OutMessage.Reset();

	if (Command.ActionTargetType == EArenaActionTargetType::None)
	{
		if (ActionDefinition.bRequiresTarget)
		{
			OutError = EArenaCommandError::InvalidRequest;
			OutMessage = TEXT("This action requires a target.");
			return false;
		}
		return true;
	}

	if (Command.TargetId.IsNone())
	{
		OutError = EArenaCommandError::InvalidRequest;
		OutMessage = TEXT("TargetId is required for the selected action target type.");
		return false;
	}

	switch (Command.ActionTargetType)
	{
	case EArenaActionTargetType::Participant:
	{
		AArenaMannequinCharacter* TargetParticipant = FindParticipant(Command.TargetId.ToString());
		if (!IsValid(TargetParticipant))
		{
			OutError = EArenaCommandError::UnknownTarget;
			OutMessage = TEXT("Action target participant was not found.");
			return false;
		}

		if (TargetParticipant == FindParticipant(Command.ActorId))
		{
			OutError = EArenaCommandError::InvalidRequest;
			OutMessage = TEXT("A participant cannot target itself with this action.");
			return false;
		}

		OutTarget = TargetParticipant;
		return true;
	}

	case EArenaActionTargetType::ArenaObject:
	{
		AActor* TargetObject = FindArenaObject(Command.TargetId);
		if (!IsValid(TargetObject)
			|| !TargetObject->GetClass()->ImplementsInterface(UArenaInteractable::StaticClass()))
		{
			OutError = EArenaCommandError::UnknownTarget;
			OutMessage = TEXT("Action target arena object was not found.");
			return false;
		}

		const TArray<FName> AllowedActions =
			IArenaInteractable::Execute_GetArenaAllowedActions(TargetObject);
		if (!AllowedActions.Contains(Command.ActionId))
		{
			OutError = EArenaCommandError::ActionNotAllowed;
			OutMessage = TEXT("The arena object does not allow this action.");
			return false;
		}

		OutTarget = TargetObject;
		return true;
	}

	case EArenaActionTargetType::None:
	default:
		OutError = EArenaCommandError::InvalidRequest;
		OutMessage = TEXT("Unsupported action target type.");
		return false;
	}
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
	if (Queue == nullptr || !Queue->bHasActiveCommand)
	{
		return;
	}

	const FArenaCommand ActiveCommand = Queue->ActiveCommand;
	switch (ActiveCommand.CommandType)
	{
	case EArenaCommandType::MoveToPoint:
		FinishActiveCommand(
			EntityId,
			bSucceeded ? EArenaCommandStatus::Completed : EArenaCommandStatus::Failed,
			bSucceeded ? EArenaCommandError::None : EArenaCommandError::UnreachableTarget,
			bSucceeded
				? TEXT("Participant reached the named point target.")
				: TEXT("Participant could not reach the named point target."));
		return;

	case EArenaCommandType::MoveToActor:
	{
		AArenaMannequinCharacter* TargetParticipant = FindParticipant(ActiveCommand.TargetId.ToString());
		if (!IsValid(TargetParticipant))
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::UnknownTarget,
				TEXT("Target participant disappeared during movement."));
			return;
		}

		if (bSucceeded)
		{
			Participant->FaceArenaTarget(TargetParticipant);
		}
		FinishActiveCommand(
			EntityId,
			bSucceeded ? EArenaCommandStatus::Completed : EArenaCommandStatus::Failed,
			bSucceeded ? EArenaCommandError::None : EArenaCommandError::UnreachableTarget,
			bSucceeded
				? TEXT("Participant reached and faced the target participant.")
				: TEXT("Participant could not reach the target participant."));
		return;
	}

	case EArenaCommandType::ApproachObject:
	{
		AActor* TargetObject = FindArenaObject(ActiveCommand.TargetId);
		if (!IsValid(TargetObject))
		{
			FinishActiveCommand(
				EntityId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::UnknownTarget,
				TEXT("Arena object disappeared during movement."));
			return;
		}

		if (bSucceeded)
		{
			Participant->FaceArenaTarget(TargetObject);
		}
		FinishActiveCommand(
			EntityId,
			bSucceeded ? EArenaCommandStatus::Completed : EArenaCommandStatus::Failed,
			bSucceeded ? EArenaCommandError::None : EArenaCommandError::UnreachableTarget,
			bSucceeded
				? TEXT("Participant reached the object interaction point and faced the object.")
				: TEXT("Participant could not reach the object interaction point."));
		return;
	}

	default:
		return;
	}
}

void AArenaParticipantManager::HandleParticipantActionFinished(
	AArenaMannequinCharacter* Participant,
	UAnimMontage* Montage,
	const bool bInterrupted)
{
	if (!IsValid(Participant) || !IsValid(Montage))
	{
		return;
	}

	const FString EntityId = Participant->GetEntityId();
	const FArenaParticipantCommandQueue* Queue = CommandQueues.Find(EntityId);
	if (Queue == nullptr
		|| !Queue->bHasActiveCommand
		|| Queue->ActiveCommand.CommandType != EArenaCommandType::PlayAction)
	{
		return;
	}

	const FArenaActionDefinition* ActionDefinition =
		FindActionDefinition(Queue->ActiveCommand.ActionId);
	if (ActionDefinition == nullptr || ActionDefinition->Montage != Montage)
	{
		return;
	}

	FinishActiveCommand(
		EntityId,
		bInterrupted ? EArenaCommandStatus::Failed : EArenaCommandStatus::Completed,
		bInterrupted ? EArenaCommandError::ActionInterrupted : EArenaCommandError::None,
		bInterrupted
			? TEXT("The action montage was interrupted before completion.")
			: TEXT("The action montage completed."));
}

void AArenaParticipantManager::HandleParticipantActionEvent(
	AArenaMannequinCharacter* Participant,
	const FName EventId)
{
	if (!IsValid(Participant) || EventId.IsNone())
	{
		return;
	}

	const FArenaParticipantCommandQueue* Queue = CommandQueues.Find(Participant->GetEntityId());
	const FString RequestId = Queue != nullptr
		&& Queue->bHasActiveCommand
		&& Queue->ActiveCommand.CommandType == EArenaCommandType::PlayAction
		? Queue->ActiveCommand.RequestId
		: FString();

	UE_LOG(
		LogArenaCommands,
		Log,
		TEXT("ActionEvent Request='%s' Actor='%s' Event='%s'"),
		*RequestId,
		*Participant->GetEntityId(),
		*EventId.ToString());
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
	AbortCommandsTargetingParticipant(DestroyedEntityId);
}

void AArenaParticipantManager::HandleArenaObjectDestroyed(AActor* DestroyedActor)
{
	FName DestroyedObjectId = NAME_None;
	for (const TPair<FName, TObjectPtr<AActor>>& Entry : ArenaObjects)
	{
		if (Entry.Value.Get() == DestroyedActor)
		{
			DestroyedObjectId = Entry.Key;
			break;
		}
	}

	if (!DestroyedObjectId.IsNone())
	{
		ArenaObjects.Remove(DestroyedObjectId);
	}
}
