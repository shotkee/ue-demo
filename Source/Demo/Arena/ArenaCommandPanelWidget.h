// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ArenaCommandTypes.h"
#include "Components/ComboBoxString.h"
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ArenaCommandPanelWidget.generated.h"

class AArenaParticipantManager;
class UButton;
class UEditableTextBox;
class UHorizontalBox;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWidget;

UCLASS()
class DEMO_API UArenaCommandComboBoxString : public UComboBoxString
{
	GENERATED_BODY()

public:
	UArenaCommandComboBoxString(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class DEMO_API UArenaCommandPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void InitializeWithManager(AArenaParticipantManager* InManager);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	void BuildPanel();
	UHorizontalBox* AddControlRow(UVerticalBox* Parent, const FString& Label, UWidget* Control);
	UTextBlock* CreateTextBlock(const FString& Text, int32 FontSize = 12) const;
	void ConfigureEditableTextBox(UEditableTextBox* TextBox) const;
	void RefreshDynamicOptions();
	void RefreshTargetOptions();
	void RefreshControlVisibility();
	void SetComboOptions(UComboBoxString* ComboBox, const TArray<FString>& Options, const FString& PreferredOption = FString());
	EArenaCommandType GetSelectedCommandType() const;
	EArenaMovementMode GetSelectedMovementMode() const;
	EArenaActionTargetType GetSelectedActionTargetType() const;
	FString GenerateRequestId();
	void AppendLogRecord(const FArenaCommandStateRecord& StateRecord);
	void SetCurrentStatus(const FArenaCommandStateRecord& StateRecord);
	static FString EnumDisplayName(const UEnum* Enum, int64 Value);

	UFUNCTION()
	void HandleSendClicked();

	UFUNCTION()
	void HandleRefreshClicked();

	UFUNCTION()
	void HandleTogglePanelClicked();

	UFUNCTION()
	void HandleCommandSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleActionTargetTypeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleParticipantSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void HandleCommandStatusChanged(FArenaCommandStateRecord StateRecord);

	UPROPERTY(Transient)
	TObjectPtr<AArenaParticipantManager> Manager;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NewEntityIdInput;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> DisplayNameInput;

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> ParticipantComboBox;

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> CommandComboBox;

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> ActionTargetTypeComboBox;

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> TargetComboBox;

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> ActionComboBox;

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> InteractionPointInput;

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> MovementModeComboBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CurrentRequestText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CurrentStatusText;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> LogScrollBox;

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> PanelSizeBox;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> PanelBody;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TogglePanelButtonText;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> NewEntityIdRow;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> DisplayNameRow;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> ParticipantRow;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> ActionTargetTypeRow;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> TargetRow;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> ActionRow;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> InteractionPointRow;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> MovementModeRow;

	FString LastSubmittedRequestId;
	bool bIsPanelCollapsed = true;
	int32 NextRequestSequence = 1;
	int32 MaximumVisibleLogEntries = 40;
};
