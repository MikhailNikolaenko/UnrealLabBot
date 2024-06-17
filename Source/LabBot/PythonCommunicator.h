#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Sound/SoundWave.h"
#include "PythonCommunicator.generated.h"

USTRUCT(BlueprintType)
struct FResponseResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    FString text;

    UPROPERTY(BlueprintReadOnly)
    TArray<uint8> audio;

    UPROPERTY(BlueprintReadOnly)
    USoundWave* SoundWave;  // Add SoundWave pointer

    UPROPERTY(BlueprintReadOnly)
    TArray<FString> AnimationTags;
    
    // Array of sound wave pointers
    UPROPERTY(BlueprintReadOnly)
    TArray<USoundWave*> SoundWaves;

    // Array of durations corresponding to each sound wave
    UPROPERTY(BlueprintReadOnly)
    TArray<FString> Durations;

};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnResponseReady, const FResponseResult&, ResponseData);

UCLASS()
class LABBOT_API APythonCommunicator : public AActor
{
    GENERATED_BODY()

public:
    // Sets default values for this actor's properties
    APythonCommunicator();

    UFUNCTION(BlueprintCallable, Category = "WebSocket")
    void SendAnimationEnd();

protected:
    // Called when the game starts or when spawned
    virtual void BeginPlay() override;

    // Called when the game ends or the actor is destroyed
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    // Called every frame
    virtual void Tick(float DeltaTime) override;

    // Delegate to be called when we receive machine data
    UPROPERTY(BlueprintAssignable, Category = "Web")
    FOnResponseReady OnResponseReady;

private:
    TSharedPtr<IWebSocket> WebSocket;
    bool bWebSocketClosed;
    TArray<uint8> AccumulatedAudio;  // Variable to store accumulated audio data
    FResponseResult AccumulatedResponse;
    FResponseResult BackupResponse;

    void StartWebSocketServer();
    void StopWebSocketServer();
    void OnWebSocketConnected();
    void OnWebSocketMessageReceived(const FString& Message);
    bool FillSoundWave(USoundWave* SoundWave, const TArray<uint8>& RawFileData);
    void OnWebSocketConnectionError(const FString& Error);
    void OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean);

    USoundWave* CreateSoundWaveFromBytes(const TArray<uint8>& AudioBytes);  // Function to create SoundWave from WAV file

};