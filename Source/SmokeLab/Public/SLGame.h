#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "SLGame.generated.h"

UENUM()
enum class ESLRound : uint8 { Playing, Planted, Lost };

UCLASS()
class SMOKELAB_API ASLGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	static ASLGameState* Get(const UObject* Ctx);
	UPROPERTY(Replicated) ESLRound Round = ESLRound::Playing;
	UPROPERTY(Replicated) float RoundStart = 0.f;
	UPROPERTY(Replicated) float RoundEnd = 0.f;
	UPROPERTY(Replicated) int32 BotsAlive = 3;
	UPROPERTY(Replicated) int32 Attempt = 1;
	/** Recent events for the kill feed ("穿烟" flag etc.), newest last. */
	UPROPERTY(Replicated) TArray<FString> Feed;
	void AddFeed(const FString& S);
};

UCLASS()
class SMOKELAB_API ASLGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ASLGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	void ResetRound();
	void OnPlanted(class ASLCharacter* By);
	void OnPlayerDied(class ASLCharacter* Who);
private:
	void SpawnBots();
	TArray<TWeakObjectPtr<class ASLBot>> Bots;
	int32 PlayersJoined = 0;
	float LostTimer = -1.f;
};
