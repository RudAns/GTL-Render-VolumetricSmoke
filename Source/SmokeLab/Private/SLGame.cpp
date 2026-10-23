#include "SLGame.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "SLBot.h"
#include "SLCharacter.h"
#include "SLGrenade.h"
#include "SLLevel.h"
#include "SLPlayerController.h"
#include "SLSmoke.h"
#include "SLSmokeManager.h"

void ASLGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASLGameState, Round);
	DOREPLIFETIME(ASLGameState, RoundStart);
	DOREPLIFETIME(ASLGameState, RoundEnd);
	DOREPLIFETIME(ASLGameState, BotsAlive);
	DOREPLIFETIME(ASLGameState, Attempt);
	DOREPLIFETIME(ASLGameState, Feed);
}

ASLGameState* ASLGameState::Get(const UObject* Ctx)
{
	return Ctx && Ctx->GetWorld() ? Ctx->GetWorld()->GetGameState<ASLGameState>() : nullptr;
}

void ASLGameState::AddFeed(const FString& S)
{
	Feed.Add(S);
	while (Feed.Num() > 5) Feed.RemoveAt(0);
}

ASLGameMode::ASLGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ASLCharacter::StaticClass();
	PlayerControllerClass = ASLPlayerController::StaticClass();
	GameStateClass = ASLGameState::StaticClass();
}

void ASLGameMode::BeginPlay()
{
	Super::BeginPlay();
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<ASLSmokeManager>(FVector::ZeroVector, FRotator::ZeroRotator, P);
	SpawnBots();
	if (ASLGameState* GS = GetGameState<ASLGameState>()) GS->RoundStart = GetWorld()->GetTimeSeconds();
}

void ASLGameMode::SpawnBots()
{
	ASLLevel* L = ASLLevel::Get(this);
	if (!L) return;
	const TArray<FTransform>& Spots = L->Spots().Sentries;
	for (int32 I = 0; I < Spots.Num(); ++I)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASLBot* B = GetWorld()->SpawnActor<ASLBot>(Spots[I].GetLocation(), Spots[I].Rotator(), P);
		if (B) { B->Setup(I, Spots[I], I == 1); Bots.Add(B); }
	}
}

AActor* ASLGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	return Super::ChoosePlayerStart_Implementation(Player);
}

void ASLGameMode::RestartPlayer(AController* NewPlayer)
{
	ASLLevel* L = ASLLevel::Get(this);
	const bool bSecond = NewPlayer && NewPlayer->PlayerState && GetGameState<AGameStateBase>()->PlayerArray.IndexOfByKey(NewPlayer->PlayerState) == 1;
	const FVector Loc = L ? (bSecond ? L->Spots().PlayerStart2 : L->Spots().PlayerStart) : FVector(0, 0, 200);
	RestartPlayerAtTransform(NewPlayer, FTransform(FRotator(0, 20, 0), Loc));
}

void ASLGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	ASLGameState* GS = GetGameState<ASLGameState>();
	if (!GS) return;
	// all players down -> round lost (players can restart from the HUD)
	if (GS->Round == ESLRound::Playing)
	{
		bool bAny = false, bAlive = false;
		for (TActorIterator<ASLCharacter> It(GetWorld()); It; ++It) { bAny = true; bAlive |= !It->IsDead(); }
		if (bAny && !bAlive) { GS->Round = ESLRound::Lost; GS->RoundEnd = GetWorld()->GetTimeSeconds(); }
	}
}

void ASLGameMode::OnPlanted(ASLCharacter* By)
{
	ASLGameState* GS = GetGameState<ASLGameState>();
	if (!GS || GS->Round != ESLRound::Playing) return;
	GS->Round = ESLRound::Planted;
	GS->RoundEnd = GetWorld()->GetTimeSeconds();
	GS->AddFeed(TEXT("炸弹已安放：进攻成功"));
}

void ASLGameMode::OnPlayerDied(ASLCharacter* Who)
{
	if (ASLGameState* GS = GetGameState<ASLGameState>()) GS->AddFeed(Who && Who->bThroughSmokeLastHit ? TEXT("你被击倒（对方穿烟射击）") : TEXT("你被击倒"));
}

void ASLGameMode::ResetRound()
{
	if (ASLSmokeManager* M = ASLSmokeManager::Get(this)) M->ResetRound();
	for (TActorIterator<ASLGrenade> It(GetWorld()); It; ++It) It->Destroy();
	for (const TWeakObjectPtr<ASLBot>& B : Bots) if (B.IsValid()) B->Revive();
	if (ASLLevel* L = ASLLevel::Get(this))
		if (ASLDoor* D = L->GetDoor()) if (D->bOpen) D->ServerToggle();
	ASLGameState* GS = GetGameState<ASLGameState>();
	int32 Idx = 0;
	for (TActorIterator<ASLCharacter> It(GetWorld()); It; ++It)
	{
		ASLCharacter* C = *It;
		C->Health = 100.f; C->Ammo = 30; C->Reserve = 120; C->Smokes = 3; C->HEs = 2; C->PlantProgress = 0.f;
		ASLLevel* L = ASLLevel::Get(this);
		const FVector Loc = L ? (Idx++ == 0 ? L->Spots().PlayerStart : L->Spots().PlayerStart2) : FVector(0, 0, 200);
		C->TeleportTo(Loc, FRotator(0, 20, 0));
		if (AController* Ctl = C->GetController()) Ctl->SetControlRotation(FRotator(0, 20, 0));
		C->OnRep_Health();
		C->ForceNetUpdate();
	}
	if (GS)
	{
		GS->Round = ESLRound::Playing;
		GS->RoundStart = GetWorld()->GetTimeSeconds();
		GS->BotsAlive = Bots.Num();
		++GS->Attempt;
		GS->Feed.Reset();
	}
}
