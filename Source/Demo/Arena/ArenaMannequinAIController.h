// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ArenaMannequinAIController.generated.h"

UCLASS()
class DEMO_API AArenaMannequinAIController : public AAIController
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	bool MoveToArenaLocation(const FVector& Destination, float AcceptanceRadius = 50.0f);

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	bool MoveToArenaActor(AActor* TargetActor, float AcceptanceRadius = 20.0f);

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	void StopArenaMovement();

protected:
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;
};
