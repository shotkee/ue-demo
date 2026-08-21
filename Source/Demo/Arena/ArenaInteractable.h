// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ArenaInteractable.generated.h"

UINTERFACE(BlueprintType)
class DEMO_API UArenaInteractable : public UInterface
{
	GENERATED_BODY()
};

class DEMO_API IArenaInteractable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Arena|Object")
	FName GetArenaObjectId() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Arena|Object")
	bool GetArenaInteractionPoint(FName InteractionPointId, FVector& OutWorldLocation) const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Arena|Object")
	TArray<FName> GetArenaAllowedActions() const;
};
