// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaParticipantManager.h"

#include "ArenaMannequinCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogArenaParticipants, Log, All);

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

	Participants.Remove(NormalizedEntityId);
	Participant->StopArenaMovement();
	Participant->Destroy();
	return true;
}

int32 AArenaParticipantManager::GetParticipantCount() const
{
	return Participants.Num();
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

void AArenaParticipantManager::HandleParticipantDestroyed(AActor* DestroyedActor)
{
	for (auto Iterator = Participants.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Value().Get() == DestroyedActor)
		{
			Iterator.RemoveCurrent();
			return;
		}
	}
}
