// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArenaActionTypes.generated.h"

class UAnimMontage;

USTRUCT(BlueprintType)
struct DEMO_API FArenaActionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Action")
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Action", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float PlayRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Action")
	bool bStopMovementBeforeAction = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Action")
	bool bRequiresTarget = true;
};
