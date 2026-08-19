// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ArenaMannequinCharacter.generated.h"

class UAnimMontage;

UENUM(BlueprintType)
enum class EArenaMovementMode : uint8
{
	Walk UMETA(DisplayName = "Walk"),
	Run UMETA(DisplayName = "Run")
};

UCLASS(Blueprintable)
class DEMO_API AArenaMannequinCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AArenaMannequinCharacter();

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	bool MoveToArenaLocation(const FVector& Destination, EArenaMovementMode MovementMode = EArenaMovementMode::Walk);

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	void SetArenaMovementMode(EArenaMovementMode MovementMode);

	UFUNCTION(BlueprintCallable, Category = "Arena|Movement")
	void StopArenaMovement();

	UFUNCTION(BlueprintCallable, Category = "Arena|Animation")
	float PlayArenaActionMontage(UAnimMontage* Montage, float PlayRate = 1.0f);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float WalkSpeed = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float RunSpeed = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float MoveAcceptanceRadius = 50.0f;
};
