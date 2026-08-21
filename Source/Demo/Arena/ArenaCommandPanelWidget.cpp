// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaCommandPanelWidget.h"

#include "ArenaParticipantManager.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Styling/CoreStyle.h"

namespace ArenaCommandPanel
{
	const FString SpawnCommand = TEXT("Spawn");
	const FString MoveToPointCommand = TEXT("Move To Point");
	const FString MoveToActorCommand = TEXT("Move To Actor");
	const FString PlayActionCommand = TEXT("Play Action");
	const FString ApproachObjectCommand = TEXT("Approach Object");
	const FString StopCommand = TEXT("Stop");
	const FString LeaveCommand = TEXT("Leave");

	const FString NoTarget = TEXT("None");
	const FString ParticipantTarget = TEXT("Participant");
	const FString ArenaObjectTarget = TEXT("Arena Object");

	const FString WalkMode = TEXT("Walk");
	const FString RunMode = TEXT("Run");
}

UArenaCommandComboBoxString::UArenaCommandComboBoxString(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitFont(FCoreStyle::GetDefaultFontStyle(FName(TEXT("Regular")), 13));
	InitForegroundColor(FSlateColor(FLinearColor::Black));
	SetContentPadding(FMargin(8.0f, 4.0f));

	const FSlateColorBrush NormalBrush(FLinearColor(0.92f, 0.93f, 0.95f, 1.0f));
	const FSlateColorBrush HoveredBrush(FLinearColor(0.72f, 0.82f, 0.95f, 1.0f));
	const FSlateColorBrush SelectedBrush(FLinearColor(0.08f, 0.42f, 0.85f, 1.0f));
	FTableRowStyle RowStyle = GetItemStyle();
	RowStyle
		.SetEvenRowBackgroundBrush(NormalBrush)
		.SetOddRowBackgroundBrush(NormalBrush)
		.SetEvenRowBackgroundHoveredBrush(HoveredBrush)
		.SetOddRowBackgroundHoveredBrush(HoveredBrush)
		.SetActiveBrush(SelectedBrush)
		.SetActiveHoveredBrush(SelectedBrush)
		.SetInactiveBrush(SelectedBrush)
		.SetInactiveHoveredBrush(SelectedBrush)
		.SetTextColor(FSlateColor(FLinearColor::Black))
		.SetSelectedTextColor(FSlateColor(FLinearColor::White));
	SetItemStyle(RowStyle);
}

void UArenaCommandPanelWidget::InitializeWithManager(AArenaParticipantManager* InManager)
{
	if (IsValid(Manager))
	{
		Manager->OnCommandStatusChanged.RemoveDynamic(this, &UArenaCommandPanelWidget::HandleCommandStatusChanged);
	}
	if (IsValid(WebSocketSubsystem))
	{
		WebSocketSubsystem->OnConnectionStateChanged.RemoveDynamic(
			this,
			&UArenaCommandPanelWidget::HandleWebSocketConnectionStateChanged);
	}

	Manager = InManager;
	WebSocketSubsystem = nullptr;
	if (!IsValid(Manager))
	{
		return;
	}

	Manager->OnCommandStatusChanged.AddUniqueDynamic(this, &UArenaCommandPanelWidget::HandleCommandStatusChanged);
	if (UGameInstance* GameInstance = Manager->GetGameInstance())
	{
		WebSocketSubsystem = GameInstance->GetSubsystem<UArenaWebSocketSubsystem>();
	}
	if (IsValid(WebSocketSubsystem))
	{
		WebSocketSubsystem->OnConnectionStateChanged.AddUniqueDynamic(
			this,
			&UArenaCommandPanelWidget::HandleWebSocketConnectionStateChanged);
		UpdateWebSocketConnectionState(WebSocketSubsystem->GetConnectionState());
	}
	RefreshDynamicOptions();

	if (IsValid(LogScrollBox))
	{
		LogScrollBox->ClearChildren();
		const TArray<FArenaCommandStateRecord> History = Manager->GetCommandHistory();
		const int32 FirstRecordIndex = FMath::Max(0, History.Num() - MaximumVisibleLogEntries);
		for (int32 Index = FirstRecordIndex; Index < History.Num(); ++Index)
		{
			AppendLogRecord(History[Index]);
		}
	}
}

void UArenaCommandPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildPanel();
}

void UArenaCommandPanelWidget::NativeDestruct()
{
	if (IsValid(Manager))
	{
		Manager->OnCommandStatusChanged.RemoveDynamic(this, &UArenaCommandPanelWidget::HandleCommandStatusChanged);
	}
	if (IsValid(WebSocketSubsystem))
	{
		WebSocketSubsystem->OnConnectionStateChanged.RemoveDynamic(
			this,
			&UArenaCommandPanelWidget::HandleWebSocketConnectionStateChanged);
	}

	Super::NativeDestruct();
}

void UArenaCommandPanelWidget::BuildPanel()
{
	if (!IsValid(WidgetTree) || IsValid(WidgetTree->RootWidget))
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	PanelSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelSizeBox"));
	PanelSizeBox->SetWidthOverride(540.0f);
	PanelSizeBox->SetHeightOverride(72.0f);
	UCanvasPanelSlot* PanelCanvasSlot = RootCanvas->AddChildToCanvas(PanelSizeBox);
	PanelCanvasSlot->SetPosition(FVector2D(20.0f, 20.0f));
	PanelCanvasSlot->SetAutoSize(true);

	UBorder* PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBorder"));
	PanelBorder->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.03f, 0.94f));
	PanelBorder->SetPadding(FMargin(14.0f));
	PanelSizeBox->AddChild(PanelBorder);

	UVerticalBox* PanelRootLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelRootLayout"));
	PanelBorder->AddChild(PanelRootLayout);

	UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderRow"));
	UVerticalBoxSlot* HeaderRowSlot = PanelRootLayout->AddChildToVerticalBox(HeaderRow);
	HeaderRowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));

	UTextBlock* Title = CreateTextBlock(TEXT("Arena Command Panel"), 20);
	Title->SetAutoWrapText(false);
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.9f, 1.0f, 1.0f)));
	UHorizontalBoxSlot* TitleSlot = HeaderRow->AddChildToHorizontalBox(Title);
	TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TitleSlot->SetVerticalAlignment(VAlign_Center);

	WebSocketConnectionStateText = CreateTextBlock(TEXT("WS: Disconnected"), 10);
	WebSocketConnectionStateText->SetAutoWrapText(false);
	WebSocketConnectionStateText->SetColorAndOpacity(
		FSlateColor(FLinearColor(0.65f, 0.68f, 0.72f, 1.0f)));
	USizeBox* WebSocketStateSizeBox = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(),
		TEXT("WebSocketStateSizeBox"));
	WebSocketStateSizeBox->SetWidthOverride(108.0f);
	WebSocketStateSizeBox->AddChild(WebSocketConnectionStateText);
	UHorizontalBoxSlot* WebSocketStateSlot = HeaderRow->AddChildToHorizontalBox(
		WebSocketStateSizeBox);
	WebSocketStateSlot->SetPadding(FMargin(8.0f, 4.0f, 0.0f, 0.0f));
	WebSocketStateSlot->SetVerticalAlignment(VAlign_Center);

	UButton* TogglePanelButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TogglePanelButton"));
	TogglePanelButton->SetBackgroundColor(FLinearColor(0.12f, 0.16f, 0.22f, 1.0f));
	TogglePanelButton->OnClicked.AddDynamic(this, &UArenaCommandPanelWidget::HandleTogglePanelClicked);
	TogglePanelButtonText = CreateTextBlock(TEXT("Expand"), 11);
	TogglePanelButtonText->SetAutoWrapText(false);
	TogglePanelButtonText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	TogglePanelButton->AddChild(TogglePanelButtonText);
	USizeBox* TogglePanelButtonSizeBox = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(),
		TEXT("TogglePanelButtonSizeBox"));
	TogglePanelButtonSizeBox->SetWidthOverride(76.0f);
	TogglePanelButtonSizeBox->SetHeightOverride(36.0f);
	TogglePanelButtonSizeBox->AddChild(TogglePanelButton);
	UHorizontalBoxSlot* TogglePanelButtonSlot = HeaderRow->AddChildToHorizontalBox(
		TogglePanelButtonSizeBox);
	TogglePanelButtonSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
	TogglePanelButtonSlot->SetVerticalAlignment(VAlign_Center);

	PanelBody = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelBody"));
	PanelBody->SetVisibility(ESlateVisibility::Collapsed);
	UVerticalBoxSlot* PanelBodySlot = PanelRootLayout->AddChildToVerticalBox(PanelBody);
	PanelBodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UVerticalBox* Layout = PanelBody;

	UTextBlock* Subtitle = CreateTextBlock(TEXT("Local testing through SubmitArenaCommand"), 10);
	Subtitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.6f, 0.68f, 1.0f)));
	UVerticalBoxSlot* SubtitleSlot = Layout->AddChildToVerticalBox(Subtitle);
	SubtitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	NewEntityIdInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("NewEntityIdInput"));
	ConfigureEditableTextBox(NewEntityIdInput);
	NewEntityIdInput->SetText(FText::FromString(TEXT("alice")));
	NewEntityIdInput->SetHintText(FText::FromString(TEXT("new participant ID")));
	NewEntityIdRow = AddControlRow(Layout, TEXT("New participant ID"), NewEntityIdInput);

	DisplayNameInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("DisplayNameInput"));
	ConfigureEditableTextBox(DisplayNameInput);
	DisplayNameInput->SetText(FText::FromString(TEXT("Alice")));
	DisplayNameInput->SetHintText(FText::FromString(TEXT("display name")));
	DisplayNameRow = AddControlRow(Layout, TEXT("Display name"), DisplayNameInput);

	ParticipantComboBox = WidgetTree->ConstructWidget<UArenaCommandComboBoxString>(UArenaCommandComboBoxString::StaticClass(), TEXT("ParticipantComboBox"));
	ParticipantComboBox->OnSelectionChanged.AddDynamic(this, &UArenaCommandPanelWidget::HandleParticipantSelectionChanged);
	ParticipantRow = AddControlRow(Layout, TEXT("Mannequin"), ParticipantComboBox);

	CommandComboBox = WidgetTree->ConstructWidget<UArenaCommandComboBoxString>(UArenaCommandComboBoxString::StaticClass(), TEXT("CommandComboBox"));
	CommandComboBox->AddOption(ArenaCommandPanel::SpawnCommand);
	CommandComboBox->AddOption(ArenaCommandPanel::MoveToPointCommand);
	CommandComboBox->AddOption(ArenaCommandPanel::MoveToActorCommand);
	CommandComboBox->AddOption(ArenaCommandPanel::ApproachObjectCommand);
	CommandComboBox->AddOption(ArenaCommandPanel::PlayActionCommand);
	CommandComboBox->AddOption(ArenaCommandPanel::StopCommand);
	CommandComboBox->AddOption(ArenaCommandPanel::LeaveCommand);
	CommandComboBox->SetSelectedOption(ArenaCommandPanel::SpawnCommand);
	CommandComboBox->OnSelectionChanged.AddDynamic(this, &UArenaCommandPanelWidget::HandleCommandSelectionChanged);
	AddControlRow(Layout, TEXT("Command"), CommandComboBox);

	ActionTargetTypeComboBox = WidgetTree->ConstructWidget<UArenaCommandComboBoxString>(UArenaCommandComboBoxString::StaticClass(), TEXT("ActionTargetTypeComboBox"));
	ActionTargetTypeComboBox->AddOption(ArenaCommandPanel::NoTarget);
	ActionTargetTypeComboBox->AddOption(ArenaCommandPanel::ParticipantTarget);
	ActionTargetTypeComboBox->AddOption(ArenaCommandPanel::ArenaObjectTarget);
	ActionTargetTypeComboBox->SetSelectedOption(ArenaCommandPanel::ParticipantTarget);
	ActionTargetTypeComboBox->OnSelectionChanged.AddDynamic(this, &UArenaCommandPanelWidget::HandleActionTargetTypeSelectionChanged);
	ActionTargetTypeRow = AddControlRow(Layout, TEXT("Action target type"), ActionTargetTypeComboBox);

	TargetComboBox = WidgetTree->ConstructWidget<UArenaCommandComboBoxString>(UArenaCommandComboBoxString::StaticClass(), TEXT("TargetComboBox"));
	TargetRow = AddControlRow(Layout, TEXT("Target"), TargetComboBox);

	ActionComboBox = WidgetTree->ConstructWidget<UArenaCommandComboBoxString>(UArenaCommandComboBoxString::StaticClass(), TEXT("ActionComboBox"));
	ActionRow = AddControlRow(Layout, TEXT("Action"), ActionComboBox);

	InteractionPointInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("InteractionPointInput"));
	ConfigureEditableTextBox(InteractionPointInput);
	InteractionPointInput->SetText(FText::FromString(TEXT("default")));
	InteractionPointInput->SetHintText(FText::FromString(TEXT("default")));
	InteractionPointRow = AddControlRow(Layout, TEXT("Interaction point"), InteractionPointInput);

	MovementModeComboBox = WidgetTree->ConstructWidget<UArenaCommandComboBoxString>(UArenaCommandComboBoxString::StaticClass(), TEXT("MovementModeComboBox"));
	MovementModeComboBox->AddOption(ArenaCommandPanel::WalkMode);
	MovementModeComboBox->AddOption(ArenaCommandPanel::RunMode);
	MovementModeComboBox->SetSelectedOption(ArenaCommandPanel::WalkMode);
	MovementModeRow = AddControlRow(Layout, TEXT("Movement"), MovementModeComboBox);

	UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ButtonRow"));
	UVerticalBoxSlot* ButtonRowSlot = Layout->AddChildToVerticalBox(ButtonRow);
	ButtonRowSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 8.0f));

	UButton* SendButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SendButton"));
	SendButton->SetBackgroundColor(FLinearColor(0.08f, 0.42f, 0.85f, 1.0f));
	SendButton->OnClicked.AddDynamic(this, &UArenaCommandPanelWidget::HandleSendClicked);
	SendButton->AddChild(CreateTextBlock(TEXT("Send command"), 13));
	UHorizontalBoxSlot* SendButtonSlot = ButtonRow->AddChildToHorizontalBox(SendButton);
	SendButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SendButtonSlot->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));

	UButton* RefreshButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RefreshButton"));
	RefreshButton->OnClicked.AddDynamic(this, &UArenaCommandPanelWidget::HandleRefreshClicked);
	RefreshButton->AddChild(CreateTextBlock(TEXT("Refresh lists"), 13));
	UHorizontalBoxSlot* RefreshButtonSlot = ButtonRow->AddChildToHorizontalBox(RefreshButton);
	RefreshButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RefreshButtonSlot->SetPadding(FMargin(4.0f, 0.0f, 0.0f, 0.0f));

	CurrentRequestText = CreateTextBlock(TEXT("RequestId: -"), 11);
	CurrentRequestText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.86f, 1.0f)));
	Layout->AddChildToVerticalBox(CurrentRequestText);

	CurrentStatusText = CreateTextBlock(TEXT("Status: Ready"), 11);
	CurrentStatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.85f, 0.65f, 1.0f)));
	UVerticalBoxSlot* StatusSlot = Layout->AddChildToVerticalBox(CurrentStatusText);
	StatusSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 8.0f));

	UTextBlock* LogTitle = CreateTextBlock(TEXT("Recent command events"), 13);
	LogTitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.9f, 1.0f, 1.0f)));
	Layout->AddChildToVerticalBox(LogTitle);

	LogScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("LogScrollBox"));
	UVerticalBoxSlot* LogSlot = Layout->AddChildToVerticalBox(LogScrollBox);
	LogSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LogSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

	RefreshControlVisibility();
}

UHorizontalBox* UArenaCommandPanelWidget::AddControlRow(
	UVerticalBox* Parent,
	const FString& Label,
	UWidget* Control)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	UVerticalBoxSlot* RowSlot = Parent->AddChildToVerticalBox(Row);
	RowSlot->SetPadding(FMargin(0.0f, 2.0f));

	USizeBox* LabelSizeBox = WidgetTree->ConstructWidget<USizeBox>();
	LabelSizeBox->SetWidthOverride(155.0f);
	LabelSizeBox->AddChild(CreateTextBlock(Label, 11));
	Row->AddChildToHorizontalBox(LabelSizeBox);

	USizeBox* ControlSizeBox = WidgetTree->ConstructWidget<USizeBox>();
	ControlSizeBox->SetWidthOverride(300.0f);
	ControlSizeBox->SetHeightOverride(36.0f);
	ControlSizeBox->AddChild(Control);
	Row->AddChildToHorizontalBox(ControlSizeBox);
	return Row;
}

UTextBlock* UArenaCommandPanelWidget::CreateTextBlock(const FString& Text, const int32 FontSize) const
{
	UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>();
	TextBlock->SetText(FText::FromString(Text));
	TextBlock->SetAutoWrapText(true);
	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = FontSize;
	TextBlock->SetFont(Font);
	return TextBlock;
}

void UArenaCommandPanelWidget::ConfigureEditableTextBox(UEditableTextBox* TextBox) const
{
	if (!IsValid(TextBox))
	{
		return;
	}

	FEditableTextBoxStyle Style = TextBox->GetWidgetStyle();
	const FSlateColor TextColor(FLinearColor::Black);
	Style.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	Style.SetForegroundColor(TextColor);
	Style.SetFocusedForegroundColor(TextColor);
	Style.SetReadOnlyForegroundColor(TextColor);
	Style.SetPadding(FMargin(8.0f, 4.0f));
	TextBox->SetWidgetStyle(Style);
	TextBox->SetForegroundColor(FLinearColor::Black);
}

void UArenaCommandPanelWidget::RefreshDynamicOptions()
{
	if (!IsValid(Manager) || !IsValid(ParticipantComboBox))
	{
		return;
	}

	SetComboOptions(ParticipantComboBox, Manager->GetParticipantIds());

	TArray<FString> ActionIds;
	for (const FName ActionId : Manager->GetRegisteredActionIds())
	{
		ActionIds.Add(ActionId.ToString());
	}
	SetComboOptions(ActionComboBox, ActionIds);
	RefreshTargetOptions();
}

void UArenaCommandPanelWidget::RefreshTargetOptions()
{
	if (!IsValid(Manager) || !IsValid(TargetComboBox))
	{
		return;
	}

	TArray<FString> TargetIds;
	const EArenaCommandType CommandType = GetSelectedCommandType();
	if (CommandType == EArenaCommandType::MoveToPoint)
	{
		for (const FName PointId : Manager->GetNamedPointIds())
		{
			TargetIds.Add(PointId.ToString());
		}
	}
	else if (CommandType == EArenaCommandType::MoveToActor ||
		(CommandType == EArenaCommandType::PlayAction &&
			GetSelectedActionTargetType() == EArenaActionTargetType::Participant))
	{
		const FString SelectedParticipant = IsValid(ParticipantComboBox)
			? ParticipantComboBox->GetSelectedOption()
			: FString();
		for (const FString& ParticipantId : Manager->GetParticipantIds())
		{
			if (!ParticipantId.Equals(SelectedParticipant, ESearchCase::IgnoreCase))
			{
				TargetIds.Add(ParticipantId);
			}
		}
	}
	else if (CommandType == EArenaCommandType::ApproachObject ||
		(CommandType == EArenaCommandType::PlayAction &&
			GetSelectedActionTargetType() == EArenaActionTargetType::ArenaObject))
	{
		for (const FName ObjectId : Manager->GetArenaObjectIds())
		{
			TargetIds.Add(ObjectId.ToString());
		}
	}

	SetComboOptions(TargetComboBox, TargetIds);
}

void UArenaCommandPanelWidget::RefreshControlVisibility()
{
	if (!IsValid(CommandComboBox))
	{
		return;
	}

	const EArenaCommandType CommandType = GetSelectedCommandType();
	const bool bIsSpawn = CommandType == EArenaCommandType::Spawn;
	const bool bIsAction = CommandType == EArenaCommandType::PlayAction;
	const bool bIsApproachObject = CommandType == EArenaCommandType::ApproachObject;
	const bool bUsesMovementMode =
		CommandType == EArenaCommandType::MoveToPoint ||
		CommandType == EArenaCommandType::MoveToActor ||
		CommandType == EArenaCommandType::ApproachObject;
	const bool bHasTarget =
		CommandType == EArenaCommandType::MoveToPoint ||
		CommandType == EArenaCommandType::MoveToActor ||
		CommandType == EArenaCommandType::ApproachObject ||
		(bIsAction && GetSelectedActionTargetType() != EArenaActionTargetType::None);

	NewEntityIdRow->SetVisibility(bIsSpawn ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	DisplayNameRow->SetVisibility(bIsSpawn ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ParticipantRow->SetVisibility(ESlateVisibility::Visible);
	ActionTargetTypeRow->SetVisibility(bIsAction ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	TargetRow->SetVisibility(bHasTarget ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ActionRow->SetVisibility(bIsAction ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	InteractionPointRow->SetVisibility(bIsApproachObject ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	MovementModeRow->SetVisibility(bUsesMovementMode ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UArenaCommandPanelWidget::SetComboOptions(
	UComboBoxString* ComboBox,
	const TArray<FString>& Options,
	const FString& PreferredOption)
{
	if (!IsValid(ComboBox))
	{
		return;
	}

	const FString PreviousOption = PreferredOption.IsEmpty()
		? ComboBox->GetSelectedOption()
		: PreferredOption;
	ComboBox->ClearOptions();
	for (const FString& Option : Options)
	{
		ComboBox->AddOption(Option);
	}

	if (Options.Contains(PreviousOption))
	{
		ComboBox->SetSelectedOption(PreviousOption);
	}
	else if (!Options.IsEmpty())
	{
		ComboBox->SetSelectedOption(Options[0]);
	}
}

EArenaCommandType UArenaCommandPanelWidget::GetSelectedCommandType() const
{
	const FString Selection = IsValid(CommandComboBox)
		? CommandComboBox->GetSelectedOption()
		: ArenaCommandPanel::SpawnCommand;
	if (Selection == ArenaCommandPanel::MoveToPointCommand)
	{
		return EArenaCommandType::MoveToPoint;
	}
	if (Selection == ArenaCommandPanel::MoveToActorCommand)
	{
		return EArenaCommandType::MoveToActor;
	}
	if (Selection == ArenaCommandPanel::ApproachObjectCommand)
	{
		return EArenaCommandType::ApproachObject;
	}
	if (Selection == ArenaCommandPanel::PlayActionCommand)
	{
		return EArenaCommandType::PlayAction;
	}
	if (Selection == ArenaCommandPanel::StopCommand)
	{
		return EArenaCommandType::Stop;
	}
	if (Selection == ArenaCommandPanel::LeaveCommand)
	{
		return EArenaCommandType::Leave;
	}
	return EArenaCommandType::Spawn;
}

EArenaMovementMode UArenaCommandPanelWidget::GetSelectedMovementMode() const
{
	return IsValid(MovementModeComboBox) &&
		MovementModeComboBox->GetSelectedOption() == ArenaCommandPanel::RunMode
		? EArenaMovementMode::Run
		: EArenaMovementMode::Walk;
}

EArenaActionTargetType UArenaCommandPanelWidget::GetSelectedActionTargetType() const
{
	if (!IsValid(ActionTargetTypeComboBox))
	{
		return EArenaActionTargetType::None;
	}

	const FString Selection = ActionTargetTypeComboBox->GetSelectedOption();
	if (Selection == ArenaCommandPanel::ParticipantTarget)
	{
		return EArenaActionTargetType::Participant;
	}
	if (Selection == ArenaCommandPanel::ArenaObjectTarget)
	{
		return EArenaActionTargetType::ArenaObject;
	}
	return EArenaActionTargetType::None;
}

FString UArenaCommandPanelWidget::GenerateRequestId()
{
	return FString::Printf(TEXT("ue7-local-%06d"), NextRequestSequence++);
}

void UArenaCommandPanelWidget::AppendLogRecord(const FArenaCommandStateRecord& StateRecord)
{
	if (!IsValid(LogScrollBox))
	{
		return;
	}

	const FString CommandName = EnumDisplayName(
		StaticEnum<EArenaCommandType>(),
		static_cast<int64>(StateRecord.CommandType));
	const FString StatusName = EnumDisplayName(
		StaticEnum<EArenaCommandStatus>(),
		static_cast<int64>(StateRecord.Status));
	const FString ErrorName = EnumDisplayName(
		StaticEnum<EArenaCommandError>(),
		static_cast<int64>(StateRecord.ErrorCode));

	FString Line = FString::Printf(
		TEXT("[%s] %s | %s | %s | %s"),
		*StateRecord.TimestampUtc.ToString(TEXT("%H:%M:%S")),
		*StateRecord.RequestId,
		*StateRecord.ActorId,
		*CommandName,
		*StatusName);
	if (StateRecord.ErrorCode != EArenaCommandError::None)
	{
		Line += FString::Printf(TEXT(" | %s"), *ErrorName);
	}
	if (!StateRecord.Message.IsEmpty())
	{
		Line += FString::Printf(TEXT(" | %s"), *StateRecord.Message);
	}

	UTextBlock* LogLine = CreateTextBlock(Line, 9);
	if (StateRecord.Status == EArenaCommandStatus::Rejected ||
		StateRecord.Status == EArenaCommandStatus::Failed ||
		StateRecord.Status == EArenaCommandStatus::Cancelled)
	{
		LogLine->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.46f, 0.42f, 1.0f)));
	}
	else if (StateRecord.Status == EArenaCommandStatus::Completed)
	{
		LogLine->SetColorAndOpacity(FSlateColor(FLinearColor(0.48f, 0.9f, 0.55f, 1.0f)));
	}
	else
	{
		LogLine->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.76f, 0.82f, 1.0f)));
	}
	LogScrollBox->AddChild(LogLine);

	while (LogScrollBox->GetChildrenCount() > MaximumVisibleLogEntries)
	{
		LogScrollBox->RemoveChildAt(0);
	}
	LogScrollBox->ScrollToEnd();
}

void UArenaCommandPanelWidget::SetCurrentStatus(const FArenaCommandStateRecord& StateRecord)
{
	if (!IsValid(CurrentRequestText) || !IsValid(CurrentStatusText))
	{
		return;
	}

	CurrentRequestText->SetText(FText::FromString(
		FString::Printf(TEXT("RequestId: %s"), *StateRecord.RequestId)));
	const FString StatusName = EnumDisplayName(
		StaticEnum<EArenaCommandStatus>(),
		static_cast<int64>(StateRecord.Status));
	const FString ErrorSuffix = StateRecord.ErrorCode == EArenaCommandError::None
		? FString()
		: FString::Printf(
			TEXT(" / %s"),
			*EnumDisplayName(StaticEnum<EArenaCommandError>(), static_cast<int64>(StateRecord.ErrorCode)));
	const FString MessageSuffix = StateRecord.Message.IsEmpty()
		? FString()
		: FString::Printf(TEXT(" — %s"), *StateRecord.Message);
	CurrentStatusText->SetText(FText::FromString(
		FString::Printf(TEXT("Status: %s%s%s"), *StatusName, *ErrorSuffix, *MessageSuffix)));

	const bool bIsError =
		StateRecord.Status == EArenaCommandStatus::Rejected ||
		StateRecord.Status == EArenaCommandStatus::Failed ||
		StateRecord.Status == EArenaCommandStatus::Cancelled;
	CurrentStatusText->SetColorAndOpacity(FSlateColor(
		bIsError
			? FLinearColor(1.0f, 0.46f, 0.42f, 1.0f)
			: FLinearColor(0.6f, 0.85f, 0.65f, 1.0f)));
}

void UArenaCommandPanelWidget::UpdateWebSocketConnectionState(
	const EArenaWebSocketConnectionState NewState)
{
	if (!IsValid(WebSocketConnectionStateText))
	{
		return;
	}

	FString Label;
	FLinearColor Color;
	switch (NewState)
	{
	case EArenaWebSocketConnectionState::Connecting:
		Label = TEXT("WS: Connecting");
		Color = FLinearColor(0.95f, 0.8f, 0.25f, 1.0f);
		break;
	case EArenaWebSocketConnectionState::Connected:
		Label = TEXT("WS: Connected");
		Color = FLinearColor(0.3f, 0.9f, 0.45f, 1.0f);
		break;
	case EArenaWebSocketConnectionState::Reconnecting:
		Label = TEXT("WS: Reconnecting");
		Color = FLinearColor(1.0f, 0.55f, 0.2f, 1.0f);
		break;
	case EArenaWebSocketConnectionState::Disconnected:
	default:
		Label = TEXT("WS: Disconnected");
		Color = FLinearColor(0.65f, 0.68f, 0.72f, 1.0f);
		break;
	}

	WebSocketConnectionStateText->SetText(FText::FromString(Label));
	WebSocketConnectionStateText->SetColorAndOpacity(FSlateColor(Color));
}

FString UArenaCommandPanelWidget::EnumDisplayName(const UEnum* Enum, const int64 Value)
{
	return IsValid(Enum)
		? Enum->GetDisplayNameTextByValue(Value).ToString()
		: TEXT("Unknown");
}

void UArenaCommandPanelWidget::HandleSendClicked()
{
	if (!IsValid(Manager))
	{
		CurrentStatusText->SetText(FText::FromString(TEXT("Status: Manager is unavailable")));
		return;
	}

	FArenaCommand Command;
	Command.Version = Manager->GetSupportedProtocolVersion();
	Command.RequestId = GenerateRequestId();
	Command.CommandType = GetSelectedCommandType();
	Command.MovementMode = GetSelectedMovementMode();

	if (Command.CommandType == EArenaCommandType::Spawn)
	{
		Command.ActorId = NewEntityIdInput->GetText().ToString();
		Command.DisplayName = DisplayNameInput->GetText();
	}
	else
	{
		Command.ActorId = ParticipantComboBox->GetSelectedOption();
	}

	if (Command.CommandType == EArenaCommandType::MoveToPoint ||
		Command.CommandType == EArenaCommandType::MoveToActor)
	{
		Command.TargetId = FName(*TargetComboBox->GetSelectedOption());
	}
	else if (Command.CommandType == EArenaCommandType::ApproachObject)
	{
		Command.TargetId = FName(*TargetComboBox->GetSelectedOption());
		Command.InteractionPointId = FName(*InteractionPointInput->GetText().ToString());
	}
	else if (Command.CommandType == EArenaCommandType::PlayAction)
	{
		Command.ActionId = FName(*ActionComboBox->GetSelectedOption());
		Command.ActionTargetType = GetSelectedActionTargetType();
		if (Command.ActionTargetType != EArenaActionTargetType::None)
		{
			Command.TargetId = FName(*TargetComboBox->GetSelectedOption());
		}
	}

	LastSubmittedRequestId = Command.RequestId;
	CurrentRequestText->SetText(FText::FromString(
		FString::Printf(TEXT("RequestId: %s"), *LastSubmittedRequestId)));
	Manager->SubmitArenaCommand(Command);
}

void UArenaCommandPanelWidget::HandleRefreshClicked()
{
	if (IsValid(Manager))
	{
		Manager->RefreshArenaObjects();
		RefreshDynamicOptions();
	}
}

void UArenaCommandPanelWidget::HandleTogglePanelClicked()
{
	if (!IsValid(PanelBody) || !IsValid(PanelSizeBox) || !IsValid(TogglePanelButtonText))
	{
		return;
	}

	bIsPanelCollapsed = !bIsPanelCollapsed;
	PanelBody->SetVisibility(
		bIsPanelCollapsed ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	PanelSizeBox->SetHeightOverride(bIsPanelCollapsed ? 72.0f : 720.0f);
	TogglePanelButtonText->SetText(FText::FromString(
		bIsPanelCollapsed ? TEXT("Expand") : TEXT("Collapse")));
}

void UArenaCommandPanelWidget::HandleCommandSelectionChanged(
	FString SelectedItem,
	ESelectInfo::Type SelectionType)
{
	RefreshControlVisibility();
	RefreshTargetOptions();
}

void UArenaCommandPanelWidget::HandleActionTargetTypeSelectionChanged(
	FString SelectedItem,
	ESelectInfo::Type SelectionType)
{
	RefreshControlVisibility();
	RefreshTargetOptions();
}

void UArenaCommandPanelWidget::HandleParticipantSelectionChanged(
	FString SelectedItem,
	ESelectInfo::Type SelectionType)
{
	RefreshTargetOptions();
}

void UArenaCommandPanelWidget::HandleCommandStatusChanged(FArenaCommandStateRecord StateRecord)
{
	AppendLogRecord(StateRecord);
	if (StateRecord.RequestId == LastSubmittedRequestId)
	{
		SetCurrentStatus(StateRecord);
	}

	if (StateRecord.Status == EArenaCommandStatus::Completed &&
		(StateRecord.CommandType == EArenaCommandType::Spawn ||
			StateRecord.CommandType == EArenaCommandType::Leave))
	{
		const FString PreferredParticipant = StateRecord.CommandType == EArenaCommandType::Spawn
			? StateRecord.ActorId
			: FString();
		SetComboOptions(ParticipantComboBox, Manager->GetParticipantIds(), PreferredParticipant);
		RefreshTargetOptions();
	}
}

void UArenaCommandPanelWidget::HandleWebSocketConnectionStateChanged(
	const EArenaWebSocketConnectionState NewState)
{
	UpdateWebSocketConnectionState(NewState);
}
