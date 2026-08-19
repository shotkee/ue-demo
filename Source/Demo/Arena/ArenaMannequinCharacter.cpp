// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaMannequinCharacter.h"

#include "AI/Navigation/NavigationTypes.h"
#include "ArenaMannequinAIController.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AArenaMannequinCharacter::AArenaMannequinCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->bUseControllerDesiredRotation = false;
	if (FNavMovementProperties* NavMovementProperties = Movement->GetNavMovementProperties())
	{
		NavMovementProperties->bUseAccelerationForPaths = true;
	}
	Movement->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MinAnalogWalkSpeed = 20.0f;
	Movement->BrakingDecelerationWalking = 2000.0f;

	AIControllerClass = AArenaMannequinAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

bool AArenaMannequinCharacter::MoveToArenaLocation(const FVector& Destination, const EArenaMovementMode MovementMode)
{
	SetArenaMovementMode(MovementMode);

	AArenaMannequinAIController* ArenaController = Cast<AArenaMannequinAIController>(GetController());
	if (!IsValid(ArenaController))
	{
		return false;
	}

	return ArenaController->MoveToArenaLocation(Destination, MoveAcceptanceRadius);
}

void AArenaMannequinCharacter::SetArenaMovementMode(const EArenaMovementMode MovementMode)
{
	GetCharacterMovement()->MaxWalkSpeed = MovementMode == EArenaMovementMode::Run ? RunSpeed : WalkSpeed;
}

void AArenaMannequinCharacter::StopArenaMovement()
{
	if (AArenaMannequinAIController* ArenaController = Cast<AArenaMannequinAIController>(GetController()))
	{
		ArenaController->StopArenaMovement();
	}

	GetCharacterMovement()->StopMovementImmediately();
}

float AArenaMannequinCharacter::PlayArenaActionMontage(UAnimMontage* Montage, const float PlayRate)
{
	if (!IsValid(Montage) || PlayRate <= 0.0f)
	{
		return 0.0f;
	}

	StopArenaMovement();
	return PlayAnimMontage(Montage, PlayRate);
}
