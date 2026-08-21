// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ArenaInteractable.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaInteractableObject.generated.h"

class USceneComponent;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct DEMO_API FArenaInteractionPointDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Object")
	FName PointId = FName(TEXT("default"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Object", meta = (MakeEditWidget = true, Units = "cm"))
	FVector LocalOffset = FVector(180.0f, 0.0f, 0.0f);
};

UCLASS(Blueprintable)
class DEMO_API AArenaInteractableObject : public AActor, public IArenaInteractable
{
	GENERATED_BODY()

public:
	AArenaInteractableObject();

	virtual FName GetArenaObjectId_Implementation() const override;
	virtual bool GetArenaInteractionPoint_Implementation(
		FName InteractionPointId,
		FVector& OutWorldLocation) const override;
	virtual TArray<FName> GetArenaAllowedActions_Implementation() const override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arena|Object")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arena|Object")
	TObjectPtr<UStaticMeshComponent> ObjectMesh;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Arena|Object")
	FName ObjectId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Object")
	TArray<FArenaInteractionPointDefinition> InteractionPoints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arena|Object")
	TArray<FName> AllowedActions;
};
