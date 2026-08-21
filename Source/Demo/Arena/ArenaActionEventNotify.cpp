// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaActionEventNotify.h"

#include "ArenaMannequinCharacter.h"
#include "Components/SkeletalMeshComponent.h"

void UArenaActionEventNotify::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp) || EventId.IsNone())
	{
		return;
	}

	if (AArenaMannequinCharacter* Mannequin = Cast<AArenaMannequinCharacter>(MeshComp->GetOwner()))
	{
		Mannequin->NotifyArenaActionEvent(EventId);
	}
}

FString UArenaActionEventNotify::GetNotifyName_Implementation() const
{
	return EventId.IsNone()
		? TEXT("Arena Action Event")
		: FString::Printf(TEXT("Arena: %s"), *EventId.ToString());
}
