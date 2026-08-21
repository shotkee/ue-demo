// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaMannequinCharacter.h"

#include "AI/Navigation/NavigationTypes.h"
#include "ArenaMannequinAIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	constexpr float NameLabelOutlineThickness = 1.0f;
	constexpr float NameLabelLetterSpacing = NameLabelOutlineThickness * 2.0f;
}

AArenaMannequinCharacter::AArenaMannequinCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(GetRootComponent());
	NameLabel->SetRelativeLocation(FVector(0.0f, 0.0f, 125.0f));
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetVerticalAlignment(EVRTA_TextCenter);
	NameLabel->SetWorldSize(26.0f);
	NameLabel->SetHorizSpacingAdjust(NameLabelLetterSpacing);
	NameLabel->SetTextRenderColor(FColor::White);
	NameLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NameLabel->SetGenerateOverlapEvents(false);
	NameLabel->SetCastShadow(false);
	NameLabel->SetTranslucentSortPriority(1);
	NameLabel->SetVisibility(false);

	const FVector2D OutlineOffsets[] =
	{
		FVector2D(-NameLabelOutlineThickness, 0.0f),
		FVector2D(NameLabelOutlineThickness, 0.0f),
		FVector2D(0.0f, -NameLabelOutlineThickness),
		FVector2D(0.0f, NameLabelOutlineThickness),
		FVector2D(-0.75f * NameLabelOutlineThickness, -0.75f * NameLabelOutlineThickness),
		FVector2D(-0.75f * NameLabelOutlineThickness, 0.75f * NameLabelOutlineThickness),
		FVector2D(0.75f * NameLabelOutlineThickness, -0.75f * NameLabelOutlineThickness),
		FVector2D(0.75f * NameLabelOutlineThickness, 0.75f * NameLabelOutlineThickness)
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(OutlineOffsets); ++Index)
	{
		const FName ComponentName(*FString::Printf(TEXT("NameLabelOutline%d"), Index));
		UTextRenderComponent* Outline = CreateDefaultSubobject<UTextRenderComponent>(ComponentName);
		Outline->SetupAttachment(NameLabel);
		Outline->SetRelativeLocation(FVector(-0.5f, OutlineOffsets[Index].X, OutlineOffsets[Index].Y));
		Outline->SetHorizontalAlignment(EHTA_Center);
		Outline->SetVerticalAlignment(EVRTA_TextCenter);
		Outline->SetWorldSize(26.0f);
		Outline->SetHorizSpacingAdjust(NameLabelLetterSpacing);
		Outline->SetTextRenderColor(FColor::Black);
		Outline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Outline->SetGenerateOverlapEvents(false);
		Outline->SetCastShadow(false);
		Outline->SetTranslucentSortPriority(0);
		Outline->SetVisibility(false);
		NameLabelOutlines.Add(Outline);
	}

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

void AArenaMannequinCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsValid(NameLabel) || !NameLabel->IsVisible())
	{
		return;
	}

	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!IsValid(CameraManager))
	{
		return;
	}

	FVector DirectionToCamera = CameraManager->GetCameraLocation() - NameLabel->GetComponentLocation();
	DirectionToCamera.Z = 0.0f;
	if (!DirectionToCamera.IsNearlyZero())
	{
		NameLabel->SetWorldRotation(DirectionToCamera.Rotation());
	}
}

bool AArenaMannequinCharacter::InitializeArenaParticipant(
	const FString& InEntityId,
	const FText& InDisplayName,
	const FVector& InSpawnLocation)
{
	FString NormalizedEntityId = InEntityId;
	NormalizedEntityId.TrimStartAndEndInline();

	if (!EntityId.IsEmpty() || NormalizedEntityId.IsEmpty())
	{
		return false;
	}

	EntityId = MoveTemp(NormalizedEntityId);
	SpawnLocation = InSpawnLocation;
	ActorState = EArenaActorState::Idle;

	if (!SetArenaDisplayName(InDisplayName))
	{
		SetArenaDisplayName(FText::FromString(EntityId));
	}

	return true;
}

FString AArenaMannequinCharacter::GetEntityId() const
{
	return EntityId;
}

FText AArenaMannequinCharacter::GetDisplayName() const
{
	return DisplayName;
}

EArenaActorState AArenaMannequinCharacter::GetArenaActorState() const
{
	return ActorState;
}

FVector AArenaMannequinCharacter::GetArenaSpawnLocation() const
{
	return SpawnLocation;
}

bool AArenaMannequinCharacter::SetArenaDisplayName(const FText& InDisplayName)
{
	FString NormalizedDisplayName = InDisplayName.ToString();
	NormalizedDisplayName.TrimStartAndEndInline();
	if (NormalizedDisplayName.IsEmpty())
	{
		return false;
	}

	DisplayName = FText::FromString(NormalizedDisplayName);
	NameLabel->SetText(DisplayName);
	NameLabel->SetVisibility(true);
	for (UTextRenderComponent* Outline : NameLabelOutlines)
	{
		if (IsValid(Outline))
		{
			Outline->SetText(DisplayName);
			Outline->SetVisibility(true);
		}
	}
	return true;
}

bool AArenaMannequinCharacter::MoveToArenaLocation(const FVector& Destination, const EArenaMovementMode MovementMode)
{
	SetArenaMovementMode(MovementMode);

	AArenaMannequinAIController* ArenaController = Cast<AArenaMannequinAIController>(GetController());
	if (!IsValid(ArenaController))
	{
		SetArenaActorState(EArenaActorState::Idle);
		return false;
	}

	SetArenaActorState(EArenaActorState::Moving);
	const bool bMoveAccepted = ArenaController->MoveToArenaLocation(Destination, MoveAcceptanceRadius);
	if (!bMoveAccepted && ActorState == EArenaActorState::Moving)
	{
		SetArenaActorState(EArenaActorState::Idle);
	}

	return bMoveAccepted;
}

bool AArenaMannequinCharacter::MoveToArenaActor(AActor* TargetActor, const EArenaMovementMode MovementMode)
{
	if (!IsValid(TargetActor) || TargetActor == this)
	{
		return false;
	}

	SetArenaMovementMode(MovementMode);

	AArenaMannequinAIController* ArenaController = Cast<AArenaMannequinAIController>(GetController());
	if (!IsValid(ArenaController))
	{
		SetArenaActorState(EArenaActorState::Idle);
		return false;
	}

	SetArenaActorState(EArenaActorState::Moving);
	const bool bMoveAccepted = ArenaController->MoveToArenaActor(TargetActor, ActorApproachClearance);
	if (!bMoveAccepted && ActorState == EArenaActorState::Moving)
	{
		SetArenaActorState(EArenaActorState::Idle);
	}

	return bMoveAccepted;
}

bool AArenaMannequinCharacter::FaceArenaTarget(const AActor* TargetActor)
{
	if (!IsValid(TargetActor) || TargetActor == this)
	{
		return false;
	}

	FVector DirectionToTarget = TargetActor->GetActorLocation() - GetActorLocation();
	DirectionToTarget.Z = 0.0f;
	if (DirectionToTarget.IsNearlyZero())
	{
		return false;
	}

	const FRotator TargetRotation = DirectionToTarget.Rotation();
	SetActorRotation(TargetRotation);
	if (AController* ArenaController = GetController())
	{
		ArenaController->SetControlRotation(TargetRotation);
	}
	return true;
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
	if (ActorState == EArenaActorState::Moving)
	{
		SetArenaActorState(EArenaActorState::Idle);
	}
}

float AArenaMannequinCharacter::PlayArenaActionMontage(UAnimMontage* Montage, const float PlayRate)
{
	return StartArenaActionMontage(Montage, PlayRate, true);
}

float AArenaMannequinCharacter::StartArenaActionMontage(
	UAnimMontage* Montage,
	const float PlayRate,
	const bool bStopMovementBeforeAction)
{
	if (!IsValid(Montage) || PlayRate <= 0.0f)
	{
		return 0.0f;
	}

	if (IsValid(ActiveActionMontage))
	{
		return 0.0f;
	}

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (!IsValid(AnimInstance))
	{
		return 0.0f;
	}

	if (bStopMovementBeforeAction)
	{
		StopArenaMovement();
	}

	const float Duration = PlayAnimMontage(Montage, PlayRate);
	if (Duration <= 0.0f)
	{
		return 0.0f;
	}

	ActiveActionMontage = Montage;
	SetArenaActorState(EArenaActorState::PerformingAction);

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AArenaMannequinCharacter::HandleArenaActionMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);

	return Duration;
}

void AArenaMannequinCharacter::StopArenaActionMontage(const float BlendOutTime)
{
	UAnimMontage* MontageToStop = ActiveActionMontage.Get();
	if (!IsValid(MontageToStop))
	{
		return;
	}

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (IsValid(AnimInstance) && AnimInstance->Montage_IsActive(MontageToStop))
	{
		AnimInstance->Montage_Stop(FMath::Max(0.0f, BlendOutTime), MontageToStop);
		return;
	}

	ActiveActionMontage = nullptr;
	SetArenaActorState(EArenaActorState::Idle);
	OnArenaActionFinished.Broadcast(this, MontageToStop, true);
}

void AArenaMannequinCharacter::NotifyArenaActionEvent(const FName EventId)
{
	if (!EventId.IsNone() && IsValid(ActiveActionMontage))
	{
		OnArenaActionEvent.Broadcast(this, EventId);
	}
}

void AArenaMannequinCharacter::NotifyArenaMovementFinished(const bool bSucceeded)
{
	if (ActorState == EArenaActorState::Moving)
	{
		SetArenaActorState(EArenaActorState::Idle);
	}

	OnArenaMovementFinished.Broadcast(this, bSucceeded);
}

void AArenaMannequinCharacter::SetArenaActorState(const EArenaActorState NewState)
{
	ActorState = NewState;
}

void AArenaMannequinCharacter::HandleArenaActionMontageEnded(UAnimMontage* Montage, const bool bInterrupted)
{
	if (Montage != ActiveActionMontage)
	{
		return;
	}

	ActiveActionMontage = nullptr;
	SetArenaActorState(EArenaActorState::Idle);
	OnArenaActionFinished.Broadcast(this, Montage, bInterrupted);
}
