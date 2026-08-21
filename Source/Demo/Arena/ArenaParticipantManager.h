// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ArenaCommandTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaParticipantManager.generated.h"

class AArenaMannequinCharacter;

USTRUCT()
struct FArenaParticipantCommandQueue
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FArenaCommand> PendingCommands;

	UPROPERTY()
	FArenaCommand ActiveCommand;

	UPROPERTY()
	bool bHasActiveCommand = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FArenaCommandStatusChangedSignature,
	FArenaCommandStateRecord,
	StateRecord);

UCLASS(Blueprintable)
class DEMO_API AArenaParticipantManager : public AActor
{
	GENERATED_BODY()

public:
	AArenaParticipantManager();

	UFUNCTION(BlueprintCallable, Category = "Arena|Participants")
	AArenaMannequinCharacter* SpawnParticipant(const FString& EntityId, const FText& DisplayName);

	UFUNCTION(BlueprintPure, Category = "Arena|Participants")
	AArenaMannequinCharacter* FindParticipant(const FString& EntityId) const;

	UFUNCTION(BlueprintCallable, Category = "Arena|Participants")
	bool SetParticipantDisplayName(const FString& EntityId, const FText& NewDisplayName);

	UFUNCTION(BlueprintCallable, Category = "Arena|Participants")
	bool RemoveParticipant(const FString& EntityId);

	UFUNCTION(BlueprintPure, Category = "Arena|Participants")
	int32 GetParticipantCount() const;

	UFUNCTION(BlueprintCallable, Category = "Arena|Commands")
	FArenaCommandResult SubmitArenaCommand(const FArenaCommand& Command);

	UFUNCTION(BlueprintCallable, Category = "Arena|Commands")
	FArenaCommandResult SubmitSpawnCommand(const FString& RequestId, const FString& EntityId, const FText& DisplayName);

	UFUNCTION(BlueprintCallable, Category = "Arena|Commands")
	FArenaCommandResult SubmitMoveToPointCommand(
		const FString& RequestId,
		const FString& EntityId,
		FName TargetId,
		EArenaMovementMode MovementMode = EArenaMovementMode::Walk);

	UFUNCTION(BlueprintCallable, Category = "Arena|Commands")
	FArenaCommandResult SubmitMoveToActorCommand(
		const FString& RequestId,
		const FString& EntityId,
		const FString& TargetEntityId,
		EArenaMovementMode MovementMode = EArenaMovementMode::Walk);

	UFUNCTION(BlueprintCallable, Category = "Arena|Commands")
	FArenaCommandResult SubmitApproachObjectCommand(
		const FString& RequestId,
		const FString& EntityId,
		FName ObjectId,
		FName InteractionPointId,
		EArenaMovementMode MovementMode = EArenaMovementMode::Walk);

	UFUNCTION(BlueprintCallable, Category = "Arena|Commands")
	FArenaCommandResult SubmitStopCommand(const FString& RequestId, const FString& EntityId);

	UFUNCTION(BlueprintCallable, Category = "Arena|Commands")
	FArenaCommandResult SubmitLeaveCommand(const FString& RequestId, const FString& EntityId);

	UFUNCTION(BlueprintPure, Category = "Arena|Commands")
	TArray<FArenaCommandStateRecord> GetCommandHistory() const;

	UFUNCTION(BlueprintPure, Category = "Arena|Commands")
	int32 GetQueuedCommandCount(const FString& EntityId) const;

	UFUNCTION(BlueprintPure, Category = "Arena|Commands")
	bool IsParticipantExecutingCommand(const FString& EntityId) const;

	UFUNCTION(BlueprintPure, Category = "Arena|Commands")
	int32 GetSupportedProtocolVersion() const;

	UFUNCTION(BlueprintCallable, Category = "Arena|Objects")
	int32 RefreshArenaObjects();

	UFUNCTION(BlueprintPure, Category = "Arena|Objects")
	AActor* FindArenaObject(FName ObjectId) const;

	UFUNCTION(BlueprintPure, Category = "Arena|Objects")
	int32 GetArenaObjectCount() const;

	UPROPERTY(BlueprintAssignable, Category = "Arena|Commands")
	FArenaCommandStatusChangedSignature OnCommandStatusChanged;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Participants")
	TSubclassOf<AArenaMannequinCharacter> MannequinClass;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Arena|Spawning")
	TObjectPtr<AActor> SpawnOrigin;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Spawning", meta = (ClampMin = "0.0", Units = "cm"))
	float SpawnRadius = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Spawning", meta = (ClampMin = "0.0", Units = "cm"))
	float MinimumParticipantDistance = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Spawning", meta = (ClampMin = "0.0", Units = "cm"))
	float SpawnClearance = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Spawning", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumParticipants = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Spawning", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumSpawnAttempts = 30;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Arena|Commands")
	TMap<FName, TObjectPtr<AActor>> NamedPointTargets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Commands", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumQueuedCommandsPerParticipant = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Commands", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaximumCommandHistoryEntries = 200;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Objects", meta = (Units = "cm"))
	FVector InteractionPointProjectionExtent = FVector(100.0f, 100.0f, 250.0f);

private:
	static constexpr int32 SupportedProtocolVersion = 1;

	bool TryFindSpawnLocation(FVector& OutSpawnLocation) const;
	bool IsSpawnLocationFree(const FVector& CandidateLocation, float CapsuleRadius, float CapsuleHalfHeight) const;
	bool TryResolveArenaObjectInteractionPoint(
		FName ObjectId,
		FName InteractionPointId,
		AActor*& OutObject,
		FVector& OutWorldLocation) const;
	bool TryProjectInteractionPointToNavigation(
		const FVector& InteractionPoint,
		FVector& OutNavigationLocation) const;
	static FString NormalizeEntityId(const FString& EntityId);
	static FString NormalizeRequestId(const FString& RequestId);
	static FName NormalizeNameId(FName NameId);

	FArenaCommandResult RejectCommand(
		const FArenaCommand& Command,
		EArenaCommandError ErrorCode,
		const FString& Message);
	FArenaCommandResult ProcessSpawnCommand(const FArenaCommand& Command);
	FArenaCommandResult ProcessStopCommand(const FArenaCommand& Command, AArenaMannequinCharacter* Participant);
	void TryStartNextCommand(const FString& EntityId);
	void ExecuteActiveCommand(const FString& EntityId, AArenaMannequinCharacter* Participant);
	void FinishActiveCommand(
		const FString& EntityId,
		EArenaCommandStatus Status,
		EArenaCommandError ErrorCode,
		const FString& Message);
	void CancelParticipantCommands(
		const FString& EntityId,
		EArenaCommandError ErrorCode,
		const FString& Message);
	bool DestroyRegisteredParticipant(const FString& EntityId, AArenaMannequinCharacter* Participant);
	void RecordCommandState(
		const FArenaCommand& Command,
		EArenaCommandStatus Status,
		EArenaCommandError ErrorCode = EArenaCommandError::None,
		const FString& Message = FString());
	static FArenaCommandResult MakeCommandResult(
		bool bAccepted,
		EArenaCommandStatus Status,
		EArenaCommandError ErrorCode,
		const FString& Message);

	void HandleParticipantMovementFinished(AArenaMannequinCharacter* Participant, bool bSucceeded);

	UFUNCTION()
	void HandleParticipantDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void HandleArenaObjectDestroyed(AActor* DestroyedActor);

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<AArenaMannequinCharacter>> Participants;

	UPROPERTY(Transient)
	TMap<FString, FArenaParticipantCommandQueue> CommandQueues;

	UPROPERTY(Transient)
	TSet<FString> SeenRequestIds;

	UPROPERTY(Transient)
	TArray<FArenaCommandStateRecord> CommandHistory;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<AActor>> ArenaObjects;
};
