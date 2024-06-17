#include "PythonCommunicator.h"
#include "Engine/Engine.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Sound/SoundWave.h"
#include "AudioDecompress.h"
#include "AudioDevice.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "AudioDevice.h"
#include "Misc/SecureHash.h"


// Sets default values
APythonCommunicator::APythonCommunicator()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = true;
}

void APythonCommunicator::SendAnimationEnd()
{
    // Send a confirmation message back to the WebSocket server
    FString confirmationMessage = TEXT("{\"type\": \"animation_end_ack\", \"data\": \"Recieved animation request.\"}");
    WebSocket->Send(confirmationMessage);
}

// Called when the game starts or when spawned
void APythonCommunicator::BeginPlay()
{
    Super::BeginPlay();
    StartWebSocketServer();
}

// Called every frame
void APythonCommunicator::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

// Called when the game ends or when the actor is destroyed
void APythonCommunicator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);
    StopWebSocketServer();

    //// Wait until the WebSocket is closed
    //while (!bWebSocketClosed)
    //{
    //    FPlatformProcess::Sleep(0.1f);
    //}
}

void APythonCommunicator::StartWebSocketServer()
{
    if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
    {
        FModuleManager::Get().LoadModule("WebSockets");
    }

    UE_LOG(LogTemp, Log, TEXT("Starting WebSocket..."));
    WebSocket = FWebSocketsModule::Get().CreateWebSocket(TEXT("ws://localhost:6789"));

    if (!WebSocket.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create WebSocket"));
        return;
    }

    WebSocket->OnConnected().AddUObject(this, &APythonCommunicator::OnWebSocketConnected);
    WebSocket->OnConnectionError().AddUObject(this, &APythonCommunicator::OnWebSocketConnectionError);
    WebSocket->OnClosed().AddUObject(this, &APythonCommunicator::OnWebSocketClosed);
    WebSocket->OnMessage().AddUObject(this, &APythonCommunicator::OnWebSocketMessageReceived);

    WebSocket->Connect();
}

void APythonCommunicator::StopWebSocketServer()
{
    if (WebSocket.IsValid())
    {
        UE_LOG(LogTemp, Log, TEXT("Closing WebSocket..."));
        WebSocket->Close();
    }
}

void APythonCommunicator::OnWebSocketConnected()
{
    UE_LOG(LogTemp, Log, TEXT("WebSocket connected"));
    // Send initial message to the server to register the connection
    FString initialMessage = TEXT("{\"name\": \"UnrealClient\"}");
    WebSocket->Send(initialMessage);
}

void APythonCommunicator::OnWebSocketConnectionError(const FString& Error)
{
    UE_LOG(LogTemp, Error, TEXT("WebSocket connection error: %s"), *Error);
    bWebSocketClosed = true;
}

void APythonCommunicator::OnWebSocketClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    UE_LOG(LogTemp, Log, TEXT("WebSocket closed: %s"), *Reason);
    bWebSocketClosed = true;

}

// Function to compute checksum
FString ComputeChecksum(const TArray<uint8>& Data)
{
    FSHA1 HashState;
    HashState.Update(Data.GetData(), Data.Num());
    HashState.Final();

    uint8 Hash[FSHA1::DigestSize];
    HashState.GetHash(Hash);

    FString Checksum;
    for (uint8 Byte : Hash)
    {
        Checksum += FString::Printf(TEXT("%02x"), Byte);
    }

    return Checksum;
}

void APythonCommunicator::OnWebSocketMessageReceived(const FString& Message)
{
    UE_LOG(LogTemp, Log, TEXT("Received message: %s"), *Message);

    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        FResponseResult ResponseData;

        FString RequestType = JsonObject->GetStringField("type");
        if (RequestType == "audio")
        {
            FString AudioData = JsonObject->GetStringField("data");
            TArray<uint8> AudioBytes;
            FBase64::Decode(AudioData, AudioBytes);
            AccumulatedAudio.Append(AudioBytes);
            UE_LOG(LogTemp, Log, TEXT("Received chunk: %s"), *AudioData);
        }
        else if (RequestType == "audio_end")
        {
            //// Create a SoundWave from the accumulated audio bytes
            //FString Checksum = JsonObject->GetStringField("checksum");
            //FString ComputedChecksum = ComputeChecksum(AccumulatedAudio);

            //if (Checksum == ComputedChecksum)
            //{
            //    UE_LOG(LogTemp, Log, TEXT("Checksum matched: %s"), *Checksum);
            //}
            //else
            //{
            //    UE_LOG(LogTemp, Error, TEXT("Checksum mismatch: received %s, computed %s"), *Checksum, *ComputedChecksum);
            //}
                
                USoundWave* SoundWave = CreateSoundWaveFromBytes(AccumulatedAudio);
                if (SoundWave)
                {
                    ResponseData.SoundWave = SoundWave;
                    AccumulatedResponse.SoundWave = SoundWave;
                    UE_LOG(LogTemp, Log, TEXT("SoundWave created successfully"));
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to create SoundWave"));
                }

                ResponseData.audio = AccumulatedAudio;
                AccumulatedResponse.SoundWaves.Add(SoundWave);
                AccumulatedResponse.Durations.Add(JsonObject->GetStringField("duration"));
                AccumulatedAudio.Empty();  // Clear the accumulated data for the next audio
                UE_LOG(LogTemp, Log, TEXT("Cleared accumulated data"));

                // Send a confirmation message back to the WebSocket server
                FString confirmationMessage = TEXT("{\"type\": \"audio_end_ack\", \"data\": \"Audio processing completed.\"}");
                WebSocket->Send(confirmationMessage);
            }
        else if (RequestType == "response_end")
        {
            // Send a confirmation message back to the WebSocket server
            FString confirmationMessage = TEXT("{\"type\": \"response_end_ack\", \"data\": \"Recieved end of message.\"}");
            WebSocket->Send(confirmationMessage);

            BackupResponse = AccumulatedResponse;
            OnResponseReady.Broadcast(BackupResponse);
            AccumulatedResponse.SoundWaves.Reset();
            AccumulatedResponse.AnimationTags.Reset();
            AccumulatedResponse.Durations.Reset();
        }
        else if (RequestType == "animation")
        {
            FString Facial_Expression = JsonObject->GetStringField("facial_expression");
            if (Facial_Expression == "True") {
                AccumulatedResponse.AnimationTags.Add(JsonObject->GetStringField("data"));
            }
            else if (Facial_Expression == "False") {
                ResponseData.text = JsonObject->GetStringField("data");
                OnResponseReady.Broadcast(ResponseData);
            }

            // Send a confirmation message back to the WebSocket server
            FString confirmationMessage = TEXT("{\"type\": \"animation_ack\", \"data\": \"Recieved animation request.\"}");
            WebSocket->Send(confirmationMessage);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse JSON message"));
    }
}

bool APythonCommunicator::FillSoundWave(USoundWave* SoundWave, const TArray<uint8>& RawFileData)
{
    if (!SoundWave || RawFileData.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid SoundWave pointer or empty RawFileData"));
        return false;
    }

    // Print out the first few bytes of the RawFileData
    FString FirstBytes;
    for (int32 i = 0; i < FMath::Min(RawFileData.Num(), 64); ++i)
    {
        FirstBytes += FString::Printf(TEXT("%02x "), RawFileData[i]);
    }
    UE_LOG(LogTemp, Log, TEXT("First bytes of RawFileData: %s"), *FirstBytes);

    FWaveModInfo WaveInfo;
    FString ErrorReason;
    if (!WaveInfo.ReadWaveInfo(RawFileData.GetData(), RawFileData.Num(), &ErrorReason))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to read wave info: %s. Data size: %d"), *ErrorReason, RawFileData.Num());
        return false;
    }

    if (!WaveInfo.SampleDataStart || !WaveInfo.pChannels || !WaveInfo.pSamplesPerSec)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid wave info data pointers"));
        return false;
    }

    SoundWave->RawPCMDataSize = WaveInfo.SampleDataSize;
    SoundWave->RawPCMData = static_cast<uint8*>(FMemory::Malloc(SoundWave->RawPCMDataSize));
    if (!SoundWave->RawPCMData)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to allocate memory for PCM data"));
        return false;
    }

    FMemory::Memcpy(SoundWave->RawPCMData, WaveInfo.SampleDataStart, SoundWave->RawPCMDataSize);

    SoundWave->Duration = static_cast<float>(SoundWave->RawPCMDataSize) / (*WaveInfo.pChannels * *WaveInfo.pSamplesPerSec * sizeof(int16));
    SoundWave->NumChannels = *WaveInfo.pChannels;
    SoundWave->SetSampleRate(*WaveInfo.pSamplesPerSec);

    UE_LOG(LogTemp, Log, TEXT("Successfully filled SoundWave with WAV data"));
    return true;
}

USoundWave* APythonCommunicator::CreateSoundWaveFromBytes(const TArray<uint8>& AudioBytes)
{
    USoundWave* SoundWave = NewObject<USoundWave>(USoundWave::StaticClass());
    if (!SoundWave)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create SoundWave"));
        return nullptr;
    }

    if (!FillSoundWave(SoundWave, AudioBytes))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to fill SoundWave with data"));
        return nullptr;
    }

    return SoundWave;
}