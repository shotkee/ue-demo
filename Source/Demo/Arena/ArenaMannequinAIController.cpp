// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaMannequinAIController.h"

#include "ArenaMannequinCharacter.h"
#include "Navigation/PathFollowingComponent.h"

bool AArenaMannequinAIController::MoveToArenaLocation(const FVector& Destination, const float AcceptanceRadius)
{
	const EPathFollowingRequestResult::Type Result = MoveToLocation(
		Destination,
		FMath::Max(0.0f, AcceptanceRadius),
		true,
		true,
		true,
		false,
		nullptr,
		true);

	return Result != EPathFollowingRequestResult::Failed;
}

void AArenaMannequinAIController::StopArenaMovement()
{
	StopMovement();
}

void AArenaMannequinAIController::OnMoveCompleted(
	const FAIRequestID RequestID,
	const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);

	if (AArenaMannequinCharacter* Mannequin = Cast<AArenaMannequinCharacter>(GetPawn()))
	{
		Mannequin->NotifyArenaMovementFinished(Result.IsSuccess());
	}
}
