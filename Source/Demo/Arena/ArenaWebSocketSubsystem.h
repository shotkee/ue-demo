// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ArenaCommandTypes.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ArenaWebSocketSubsystem.generated.h"

class AArenaParticipantManager;
class IWebSocket;

UENUM(BlueprintType)
enum class EArenaWebSocketConnectionState : uint8
{
	Disconnected UMETA(DisplayName = "Disconnected"),
	Connecting UMETA(DisplayName = "Connecting"),
	Connected UMETA(DisplayName = "Connected"),
	Reconnecting UMETA(DisplayName = "Reconnecting")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FArenaWebSocketConnectionStateChangedSignature,
	EArenaWebSocketConnectionState,
	ConnectionState);

UCLASS()
class DEMO_API UArenaWebSocketSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Arena|WebSocket")
	void Connect();

	UFUNCTION(BlueprintCallable, Category = "Arena|WebSocket")
	void Disconnect();

	UFUNCTION(BlueprintPure, Category = "Arena|WebSocket")
	EArenaWebSocketConnectionState GetConnectionState() const;

	UFUNCTION(BlueprintPure, Category = "Arena|WebSocket")
	bool IsConnected() const;

	UFUNCTION(BlueprintCallable, Category = "Arena|WebSocket")
	bool SendTextMessage(const FString& Message);

	UPROPERTY(BlueprintAssignable, Category = "Arena|WebSocket")
	FArenaWebSocketConnectionStateChangedSignature OnConnectionStateChanged;

private:
	void BeginConnection();
	void ResetSocket(bool bCloseConnection);
	void ScheduleReconnect();
	void CancelReconnect();
	bool HandleReconnectTicker(float DeltaTime);
	void SetConnectionState(EArenaWebSocketConnectionState NewState);
	void FlushQueuedMessages();
	void QueueOutgoingMessage(const FString& Message);
	void ProcessIncomingMessage(const FString& Message);
	AArenaParticipantManager* ResolveParticipantManager();
	void UnbindParticipantManager();
	static bool IsAllowedServerUrl(const FString& ServerUrl, bool bAllowPrivateNetworkConnections);

	void HandleSocketConnected();
	void HandleSocketConnectionError(const FString& Error);
	void HandleSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void HandleSocketMessage(const FString& Message);

	UFUNCTION()
	void HandleCommandStatusChanged(FArenaCommandStateRecord StateRecord);

	TSharedPtr<IWebSocket> Socket;
	FTSTicker::FDelegateHandle ReconnectTickerHandle;
	TWeakObjectPtr<AArenaParticipantManager> BoundParticipantManager;
	TSet<FString> NetworkRequestIds;
	TArray<FString> QueuedOutgoingMessages;
	EArenaWebSocketConnectionState ConnectionState = EArenaWebSocketConnectionState::Disconnected;
	float CurrentReconnectDelaySeconds = 1.0f;
	bool bShouldReconnect = false;
	bool bHasAttemptedConnection = false;
};
