// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ArenaCommandTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ArenaMannequinCharacter.generated.h"

class UAnimMontage;
class UTextRenderComponent;
class AArenaMannequinCharacter;

DECLARE_MULTICAST_DELEGATE_TwoParams(FArenaMovementFinishedSignature, AArenaMannequinCharacter*, bool);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FArenaActionFinishedSignature,
	AArenaMannequinCharacter*,
	UAnimMontage*,
	bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(FArenaActionEventSignature, AArenaMannequinCharacter*, FName);

UENUM(BlueprintType)
enum class EArenaActorState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	Moving UMETA(DisplayName = "Moving"),
	PerformingAction UMETA(DisplayName = "Performing Action")
};

UCLASS(Blueprintable)
class DEMO_API AArenaMannequinCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AArenaMannequinCharacter();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "Arena|Participant")
	bool InitializeArenaParticipant(const FString& InEntityId, const FText& InDisplayName, const FVector& InSpawnLocation);

	UFUNCTION(BlueprintPure, Category = "Arena|Participant")
	FString GetEntityId() const;

	UFUNCTION(BlueprintPure, Category = "Arena|Participant")
	FText GetDisplayName() const;

	UFUNCTION(BlueprintPure, Category = "Arena|Participant")
	EArenaActorState GetArenaActorState() const;

	UFUNCTION(BlueprintPure, Category = "Arena|Participant")
	FVector GetArenaSpawnLocation() const;

	UFUNCTION(BlueprintCallable, Category = "Arena|Participant")
	bool SetArenaDisplayName(const FText& InDisplayName);

	UFUNCTION(BlueprintCallable, Category = "Arena|Participant")
	void SetArenaDisplayNameColor(FColor InDisplayNameColor);

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	bool MoveToArenaLocation(const FVector& Destination, EArenaMovementMode MovementMode = EArenaMovementMode::Walk);

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	bool MoveToArenaActor(AActor* TargetActor, EArenaMovementMode MovementMode = EArenaMovementMode::Walk);

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	bool FaceArenaTarget(const AActor* TargetActor);

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	void SetArenaMovementMode(EArenaMovementMode MovementMode);

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	void StopArenaMovement();

	UFUNCTION(BlueprintCallable, Category = "Arena|Animation")
	float PlayArenaActionMontage(UAnimMontage* Montage, float PlayRate = 1.0f);

	float StartArenaActionMontage(
		UAnimMontage* Montage,
		float PlayRate,
		bool bStopMovementBeforeAction);

	UFUNCTION(BlueprintCallable, Category = "Arena|Animation")
	void StopArenaActionMontage(float BlendOutTime = 0.1f);

	UFUNCTION(BlueprintCallable, Category = "Arena|Animation")
	void NotifyArenaActionEvent(FName EventId);

	void NotifyArenaMovementFinished(bool bSucceeded);

	FArenaMovementFinishedSignature OnArenaMovementFinished;
	FArenaActionFinishedSignature OnArenaActionFinished;
	FArenaActionEventSignature OnArenaActionEvent;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arena|Participant")
	TObjectPtr<UTextRenderComponent> NameLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arena|Participant")
	TArray<TObjectPtr<UTextRenderComponent>> NameLabelOutlines;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Arena|Participant")
	FString EntityId;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Arena|Participant")
	FText DisplayName;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Arena|Participant")
	EArenaActorState ActorState = EArenaActorState::Idle;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Arena|Participant")
	FVector SpawnLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float WalkSpeed = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float RunSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float MoveAcceptanceRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float ActorApproachClearance = 20.0f;

private:
	void SetArenaActorState(EArenaActorState NewState);

	void HandleArenaActionMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveActionMontage;
};
