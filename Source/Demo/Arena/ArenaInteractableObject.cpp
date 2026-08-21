// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaInteractableObject.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AArenaInteractableObject::AArenaInteractableObject()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ObjectMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObjectMesh"));
	ObjectMesh->SetupAttachment(SceneRoot);
	ObjectMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
	ObjectMesh->SetCollisionProfileName(TEXT("BlockAll"));
	ObjectMesh->SetCanEverAffectNavigation(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		ObjectMesh->SetStaticMesh(CubeMesh.Object);
	}

	InteractionPoints.Add(FArenaInteractionPointDefinition());
	AllowedActions.Add(FName(TEXT("inspect")));
}

FName AArenaInteractableObject::GetArenaObjectId_Implementation() const
{
	return ObjectId;
}

bool AArenaInteractableObject::GetArenaInteractionPoint_Implementation(
	const FName InteractionPointId,
	FVector& OutWorldLocation) const
{
	if (InteractionPoints.IsEmpty())
	{
		return false;
	}

	const FArenaInteractionPointDefinition* SelectedPoint = nullptr;
	if (InteractionPointId.IsNone())
	{
		SelectedPoint = &InteractionPoints[0];
	}
	else
	{
		SelectedPoint = InteractionPoints.FindByPredicate(
			[InteractionPointId](const FArenaInteractionPointDefinition& Point)
			{
				return Point.PointId == InteractionPointId;
			});
	}

	if (SelectedPoint == nullptr || SelectedPoint->PointId.IsNone())
	{
		return false;
	}

	OutWorldLocation = GetActorTransform().TransformPosition(SelectedPoint->LocalOffset);
	return true;
}

TArray<FName> AArenaInteractableObject::GetArenaAllowedActions_Implementation() const
{
	return AllowedActions;
}
