// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ArenaWebSocketSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Arena WebSocket"))
class DEMO_API UArenaWebSocketSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override;

	UPROPERTY(Config, EditAnywhere, Category = "Connection")
	bool bAutoConnect = true;

	UPROPERTY(Config, EditAnywhere, Category = "Connection")
	FString ServerUrl = TEXT("ws://127.0.0.1:8080");

	UPROPERTY(Config, EditAnywhere, Category = "Reconnection", meta = (ClampMin = "0.1", Units = "s"))
	float InitialReconnectDelaySeconds = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Reconnection", meta = (ClampMin = "0.1", Units = "s"))
	float MaximumReconnectDelaySeconds = 10.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Messages", meta = (ClampMin = "1"))
	int32 MaximumQueuedOutgoingMessages = 100;
};
