// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArenaWebSocketSubsystem.h"

#include "ArenaCommandJsonProtocol.h"
#include "ArenaParticipantManager.h"
#include "ArenaWebSocketSettings.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogArenaWebSocket, Log, All);

void UArenaWebSocketSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UArenaWebSocketSettings* Settings = GetDefault<UArenaWebSocketSettings>();
	CurrentReconnectDelaySeconds = FMath::Max(0.1f, Settings->InitialReconnectDelaySeconds);
	if (Settings->bAutoConnect)
	{
		Connect();
	}
}

void UArenaWebSocketSubsystem::Deinitialize()
{
	bShouldReconnect = false;
	CancelReconnect();
	UnbindParticipantManager();
	ResetSocket(true);
	NetworkRequestIds.Empty();
	QueuedOutgoingMessages.Empty();
	SetConnectionState(EArenaWebSocketConnectionState::Disconnected);

	Super::Deinitialize();
}

void UArenaWebSocketSubsystem::Connect()
{
	bShouldReconnect = true;
	bHasAttemptedConnection = false;
	CancelReconnect();
	BeginConnection();
}

void UArenaWebSocketSubsystem::Disconnect()
{
	bShouldReconnect = false;
	bHasAttemptedConnection = false;
	CancelReconnect();
	ResetSocket(true);
	QueuedOutgoingMessages.Empty();
	SetConnectionState(EArenaWebSocketConnectionState::Disconnected);
	UE_LOG(LogArenaWebSocket, Log, TEXT("Disconnected by request."));
}

EArenaWebSocketConnectionState UArenaWebSocketSubsystem::GetConnectionState() const
{
	return ConnectionState;
}

bool UArenaWebSocketSubsystem::IsConnected() const
{
	return Socket.IsValid() && Socket->IsConnected();
}

bool UArenaWebSocketSubsystem::SendTextMessage(const FString& Message)
{
	if (Message.IsEmpty())
	{
		return false;
	}

	if (!IsConnected())
	{
		if (bShouldReconnect)
		{
			QueueOutgoingMessage(Message);
		}
		return false;
	}

	Socket->Send(Message);
	return true;
}

void UArenaWebSocketSubsystem::BeginConnection()
{
	if (!bShouldReconnect || IsConnected()
		|| ConnectionState == EArenaWebSocketConnectionState::Connecting)
	{
		return;
	}

	const UArenaWebSocketSettings* Settings = GetDefault<UArenaWebSocketSettings>();
	if (!IsLoopbackServerUrl(Settings->ServerUrl))
	{
		bShouldReconnect = false;
		SetConnectionState(EArenaWebSocketConnectionState::Disconnected);
		UE_LOG(
			LogArenaWebSocket,
			Error,
			TEXT("ServerUrl '%s' is not a loopback WebSocket URL. Use 127.0.0.1, localhost, or [::1]."),
			*Settings->ServerUrl);
		return;
	}

	ResetSocket(false);
	SetConnectionState(
		bHasAttemptedConnection
			? EArenaWebSocketConnectionState::Reconnecting
			: EArenaWebSocketConnectionState::Connecting);
	bHasAttemptedConnection = true;

	Socket = FWebSocketsModule::Get().CreateWebSocket(Settings->ServerUrl);
	Socket->SetTextMessageMemoryLimit(
		static_cast<uint64>(FArenaCommandJsonProtocol::MaximumMessageLength) * 4ULL);
	Socket->OnConnected().AddUObject(this, &UArenaWebSocketSubsystem::HandleSocketConnected);
	Socket->OnConnectionError().AddUObject(
		this,
		&UArenaWebSocketSubsystem::HandleSocketConnectionError);
	Socket->OnClosed().AddUObject(this, &UArenaWebSocketSubsystem::HandleSocketClosed);
	Socket->OnMessage().AddUObject(this, &UArenaWebSocketSubsystem::HandleSocketMessage);

	UE_LOG(LogArenaWebSocket, Log, TEXT("Connecting to %s."), *Settings->ServerUrl);
	Socket->Connect();
}

void UArenaWebSocketSubsystem::ResetSocket(const bool bCloseConnection)
{
	if (!Socket.IsValid())
	{
		return;
	}

	Socket->OnConnected().RemoveAll(this);
	Socket->OnConnectionError().RemoveAll(this);
	Socket->OnClosed().RemoveAll(this);
	Socket->OnMessage().RemoveAll(this);
	if (bCloseConnection && Socket->IsConnected())
	{
		Socket->Close(1000, TEXT("Arena client disconnecting"));
	}
	Socket.Reset();
}

void UArenaWebSocketSubsystem::ScheduleReconnect()
{
	if (!bShouldReconnect || ReconnectTickerHandle.IsValid())
	{
		return;
	}

	const UArenaWebSocketSettings* Settings = GetDefault<UArenaWebSocketSettings>();
	const float MaximumDelay = FMath::Max(
		FMath::Max(0.1f, Settings->InitialReconnectDelaySeconds),
		Settings->MaximumReconnectDelaySeconds);
	const float Delay = FMath::Clamp(CurrentReconnectDelaySeconds, 0.1f, MaximumDelay);
	CurrentReconnectDelaySeconds = FMath::Min(Delay * 2.0f, MaximumDelay);
	SetConnectionState(EArenaWebSocketConnectionState::Reconnecting);

	ReconnectTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UArenaWebSocketSubsystem::HandleReconnectTicker),
		Delay);
	UE_LOG(LogArenaWebSocket, Log, TEXT("Reconnect scheduled in %.1f second(s)."), Delay);
}

void UArenaWebSocketSubsystem::CancelReconnect()
{
	if (!ReconnectTickerHandle.IsValid())
	{
		return;
	}

	FTSTicker::RemoveTicker(ReconnectTickerHandle);
	ReconnectTickerHandle.Reset();
}

bool UArenaWebSocketSubsystem::HandleReconnectTicker(const float DeltaTime)
{
	(void)DeltaTime;
	ReconnectTickerHandle.Reset();
	BeginConnection();
	return false;
}

void UArenaWebSocketSubsystem::SetConnectionState(
	const EArenaWebSocketConnectionState NewState)
{
	if (ConnectionState == NewState)
	{
		return;
	}

	ConnectionState = NewState;
	OnConnectionStateChanged.Broadcast(ConnectionState);
}

void UArenaWebSocketSubsystem::FlushQueuedMessages()
{
	if (!IsConnected() || QueuedOutgoingMessages.IsEmpty())
	{
		return;
	}

	TArray<FString> Messages = MoveTemp(QueuedOutgoingMessages);
	QueuedOutgoingMessages.Reset();
	for (const FString& Message : Messages)
	{
		SendTextMessage(Message);
	}
}

void UArenaWebSocketSubsystem::QueueOutgoingMessage(const FString& Message)
{
	const UArenaWebSocketSettings* Settings = GetDefault<UArenaWebSocketSettings>();
	const int32 MaximumMessages = FMath::Max(1, Settings->MaximumQueuedOutgoingMessages);
	if (QueuedOutgoingMessages.Num() >= MaximumMessages)
	{
		QueuedOutgoingMessages.RemoveAt(0, QueuedOutgoingMessages.Num() - MaximumMessages + 1);
		UE_LOG(LogArenaWebSocket, Warning, TEXT("The oldest queued WebSocket response was discarded."));
	}
	QueuedOutgoingMessages.Add(Message);
}

void UArenaWebSocketSubsystem::ProcessIncomingMessage(const FString& Message)
{
	FArenaCommand Command;
	EArenaCommandError ParseError = EArenaCommandError::None;
	FString ParseMessage;
	if (!FArenaCommandJsonProtocol::TryParseCommand(
		Message,
		Command,
		ParseError,
		ParseMessage))
	{
		SendTextMessage(
			FArenaCommandJsonProtocol::SerializeResult(
				Command.RequestId,
				EArenaCommandStatus::Rejected,
				ParseError,
				ParseMessage));
		return;
	}

	AArenaParticipantManager* ParticipantManager = ResolveParticipantManager();
	if (!IsValid(ParticipantManager))
	{
		SendTextMessage(
			FArenaCommandJsonProtocol::SerializeResult(
				Command.RequestId,
				EArenaCommandStatus::Failed,
				EArenaCommandError::ExecutionFailed,
				TEXT("Arena participant manager is not available.")));
		return;
	}

	NetworkRequestIds.Add(Command.RequestId);
	ParticipantManager->SubmitArenaCommand(Command);
}

AArenaParticipantManager* UArenaWebSocketSubsystem::ResolveParticipantManager()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	if (BoundParticipantManager.IsValid()
		&& BoundParticipantManager->GetWorld() == World)
	{
		return BoundParticipantManager.Get();
	}

	UnbindParticipantManager();
	for (TActorIterator<AArenaParticipantManager> Iterator(World); Iterator; ++Iterator)
	{
		AArenaParticipantManager* ParticipantManager = *Iterator;
		if (IsValid(ParticipantManager))
		{
			BoundParticipantManager = ParticipantManager;
			ParticipantManager->OnCommandStatusChanged.AddUniqueDynamic(
				this,
				&UArenaWebSocketSubsystem::HandleCommandStatusChanged);
			return ParticipantManager;
		}
	}

	return nullptr;
}

void UArenaWebSocketSubsystem::UnbindParticipantManager()
{
	if (BoundParticipantManager.IsValid())
	{
		BoundParticipantManager->OnCommandStatusChanged.RemoveDynamic(
			this,
			&UArenaWebSocketSubsystem::HandleCommandStatusChanged);
	}
	BoundParticipantManager.Reset();
}

bool UArenaWebSocketSubsystem::IsLoopbackServerUrl(const FString& ServerUrl)
{
	FString NormalizedUrl = ServerUrl;
	NormalizedUrl.TrimStartAndEndInline();
	NormalizedUrl.ToLowerInline();

	const TArray<FString> AllowedPrefixes = {
		TEXT("ws://127.0.0.1"),
		TEXT("wss://127.0.0.1"),
		TEXT("ws://localhost"),
		TEXT("wss://localhost"),
		TEXT("ws://[::1]"),
		TEXT("wss://[::1]")};
	for (const FString& Prefix : AllowedPrefixes)
	{
		if (!NormalizedUrl.StartsWith(Prefix))
		{
			continue;
		}

		if (NormalizedUrl.Len() == Prefix.Len())
		{
			return true;
		}

		const TCHAR NextCharacter = NormalizedUrl[Prefix.Len()];
		if (NextCharacter == TEXT(':') || NextCharacter == TEXT('/'))
		{
			return true;
		}
	}

	return false;
}

void UArenaWebSocketSubsystem::HandleSocketConnected()
{
	if (!IsInGameThread())
	{
		const TWeakObjectPtr<UArenaWebSocketSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->HandleSocketConnected();
			}
		});
		return;
	}

	CancelReconnect();
	const UArenaWebSocketSettings* Settings = GetDefault<UArenaWebSocketSettings>();
	CurrentReconnectDelaySeconds = FMath::Max(0.1f, Settings->InitialReconnectDelaySeconds);
	SetConnectionState(EArenaWebSocketConnectionState::Connected);
	UE_LOG(LogArenaWebSocket, Log, TEXT("Connected to %s."), *Settings->ServerUrl);
	FlushQueuedMessages();
}

void UArenaWebSocketSubsystem::HandleSocketConnectionError(const FString& Error)
{
	if (!IsInGameThread())
	{
		const TWeakObjectPtr<UArenaWebSocketSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Error]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->HandleSocketConnectionError(Error);
			}
		});
		return;
	}

	UE_LOG(LogArenaWebSocket, Warning, TEXT("Connection error: %s"), *Error);
	ResetSocket(false);
	ScheduleReconnect();
}

void UArenaWebSocketSubsystem::HandleSocketClosed(
	const int32 StatusCode,
	const FString& Reason,
	const bool bWasClean)
{
	if (!IsInGameThread())
	{
		const TWeakObjectPtr<UArenaWebSocketSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, StatusCode, Reason, bWasClean]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->HandleSocketClosed(StatusCode, Reason, bWasClean);
			}
		});
		return;
	}

	UE_LOG(
		LogArenaWebSocket,
		Warning,
		TEXT("Connection closed (code=%d, clean=%s): %s"),
		StatusCode,
		bWasClean ? TEXT("true") : TEXT("false"),
		*Reason);
	ResetSocket(false);
	ScheduleReconnect();
}

void UArenaWebSocketSubsystem::HandleSocketMessage(const FString& Message)
{
	if (!IsInGameThread())
	{
		const TWeakObjectPtr<UArenaWebSocketSubsystem> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Message]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->HandleSocketMessage(Message);
			}
		});
		return;
	}

	ProcessIncomingMessage(Message);
}

void UArenaWebSocketSubsystem::HandleCommandStatusChanged(
	const FArenaCommandStateRecord StateRecord)
{
	if (!NetworkRequestIds.Contains(StateRecord.RequestId))
	{
		return;
	}

	SendTextMessage(FArenaCommandJsonProtocol::SerializeCommandState(StateRecord));
}
