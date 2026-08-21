// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimNotifies/AnimNotify.h"
#include "CoreMinimal.h"
#include "ArenaActionEventNotify.generated.h"

UCLASS(meta = (DisplayName = "Arena Action Event"))
class DEMO_API UArenaActionEventNotify : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Arena|Action")
	FName EventId = FName(TEXT("impact"));
};
