#pragma once

#include "CoreMinimal.h"
#include "SLCharacter.h"
#include "SLBot.generated.h"

UENUM()
enum class ESLBotState : uint8 { Watch, Engage, Suppress, Dead };

/**
 * A sentry holding an angle. Server-only brain:
 *  - sees a player when the geometry line is clear AND the smoke along it holds less than SL::BotSmokeBlockCm
 *    (the same rule CS2 gives its bots: bot_max_visible_smoke_length);
 *  - when a target it just saw disappears into smoke it sprays into the smoke (bullet tunnels),
 *  - the platform sentry throws an HE grenade at a smoke that blocks its angle.
 * Network relevancy can hide it from clients that cannot see it through smoke (fairness view).
 */
UCLASS()
class SMOKELAB_API ASLBot : public ASLSoldier
{
	GENERATED_BODY()
public:
	ASLBot();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void Tick(float Dt) override;
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;
	void Setup(int32 InIndex, const FTransform& Home, bool bThrowsHE);
	void Revive();

	UPROPERTY(Replicated) ESLBotState State = ESLBotState::Watch;
	UPROPERTY(Replicated) float HomeYaw = 0.f;
	int32 Index = 0;
	/** Smoke along the last sight check to each player (cm of full density), for the HUD's sight lines. */
	TMap<TWeakObjectPtr<AActor>, float> SightSmoke;
	TMap<TWeakObjectPtr<AActor>, bool> SightClear;	// geometry clear
	mutable TMap<const AActor*, double> RelevantUntil;
	mutable int32 CulledFor = 0;	// how many viewers this bot is currently hidden from

	/** Can this bot see target T right now (geometry + smoke)? Also outputs the smoke length. */
	bool CanSee(const ASLSoldier* T, float& OutSmokeCm, bool& bGeometryClear) const;
	/** The relevancy rule: does smoke hide every probe point of this bot from Eye? */
	bool SmokeHides(const FVector& Eye) const;

protected:
	virtual void OnDied() override;
private:
	FTransform HomeXf;
	bool bHE = false;
	int32 HELeft = 2;
	float HECooldown = 8.f;
	TWeakObjectPtr<ASLSoldier> Target;
	FVector LastSeen = FVector::ZeroVector;
	float LastSeenTime = -100.f;
	float Reaction = 0.f;
	float BurstLeft = 0.f;
	int32 BurstShots = 0;
	float ShotTimer = 0.f;
	float SuppressUntil = -100.f;
	float NextSpray = 0.f;
	void Shoot(const FVector& Aim, float HitChance);
	void ThrowHE(const FVector& At);
};
