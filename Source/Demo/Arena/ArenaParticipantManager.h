// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaParticipantManager.generated.h"

class AArenaMannequinCharacter;

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

protected:
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

private:
	bool TryFindSpawnLocation(FVector& OutSpawnLocation) const;
	bool IsSpawnLocationFree(const FVector& CandidateLocation, float CapsuleRadius, float CapsuleHalfHeight) const;
	static FString NormalizeEntityId(const FString& EntityId);

	UFUNCTION()
	void HandleParticipantDestroyed(AActor* DestroyedActor);

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<AArenaMannequinCharacter>> Participants;
};
