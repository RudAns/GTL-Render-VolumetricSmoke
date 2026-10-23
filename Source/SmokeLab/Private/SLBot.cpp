#include "SLBot.h"

#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "SLCharacter.h"
#include "SLGame.h"
#include "SLGrenade.h"
#include "SLSmoke.h"
#include "SLSmokeManager.h"

ASLBot::ASLBot()
{
	AutoPossessAI = EAutoPossessAI::Disabled;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->GravityScale = 1.f;
	SetNetUpdateFrequency(30.f);
}

void ASLBot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASLBot, State);
	DOREPLIFETIME(ASLBot, HomeYaw);
}

void ASLBot::Setup(int32 InIndex, const FTransform& Home, bool bThrowsHE)
{
	Index = InIndex;
	HomeXf = Home;
	HomeYaw = Home.Rotator().Yaw;
	bHE = bThrowsHE;
	Revive();
}

void ASLBot::Revive()
{
	Health = 100.f;
	State = ESLBotState::Watch;
	HELeft = 2;
	HECooldown = 6.f;
	Target = nullptr;
	LastSeenTime = -100.f;
	SuppressUntil = -100.f;
	SetActorLocationAndRotation(HomeXf.GetLocation(), FRotator(0, HomeYaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
	OnRep_Health();
}

bool ASLBot::CanSee(const ASLSoldier* T, float& OutSmokeCm, bool& bGeometryClear) const
{
	OutSmokeCm = 0.f;
	bGeometryClear = false;
	if (!T || T->IsDead() || IsDead()) return false;
	const FVector Eye = EyeLocation();
	const FVector Aim = T->ChestLocation();
	const FVector D = Aim - Eye;
	if (D.Size() > 5000.f) return false;
	// field of view: 130 deg around the current facing, and never further than 70 deg from the post
	const float ToYaw = D.Rotation().Yaw;
	if (FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, ToYaw)) > 65.f &&
		FMath::Abs(FMath::FindDeltaAngleDegrees(HomeYaw, ToYaw)) > 70.f) return false;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SLBotSight), false, this);
	Q.AddIgnoredActor(T);
	FHitResult Hit;
	// head or chest visible counts
	const FVector Head = T->EyeLocation();
	const bool bChest = !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Aim, ECC_Visibility, Q);
	const bool bHead = !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Head, ECC_Visibility, Q);
	bGeometryClear = bChest || bHead;
	if (!bGeometryClear) return false;
	const ASLSmokeManager* M = ASLSmokeManager::Get(this);
	if (M)
	{
		const float A = bChest ? M->SmokeLength(Eye, Aim) : 1e6f;
		const float B = bHead ? M->SmokeLength(Eye, Head) : 1e6f;
		OutSmokeCm = FMath::Min(A, B);
	}
	return OutSmokeCm < SL::BotSmokeBlockCm * 0.5f;
}

void ASLBot::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!HasAuthority() || IsDead()) return;
	ASLSmokeManager* M = ASLSmokeManager::Get(this);
	const ASLGameState* GS = ASLGameState::Get(this);
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bFire = M && M->Settings.bBotsFire && GS && GS->Round == ESLRound::Playing;

	// pick the visible player (closest)
	ASLSoldier* Best = nullptr;
	float BestD = 1e9f;
	for (TActorIterator<ASLCharacter> It(GetWorld()); It; ++It)
	{
		float Cm; bool bClear;
		const bool bSee = CanSee(*It, Cm, bClear);
		SightSmoke.Add(*It, Cm);
		SightClear.Add(*It, bClear);
		if (bSee)
		{
			const float D = FVector::Dist(It->GetActorLocation(), GetActorLocation());
			if (D < BestD) { BestD = D; Best = *It; }
		}
		else if (bClear && Target.Get() == *It && Now - LastSeenTime < 2.5f && Cm >= SL::BotSmokeBlockCm * 0.5f)
		{
			// it was just here and walked into smoke: keep suppressing that spot
			SuppressUntil = FMath::Max(SuppressUntil, LastSeenTime + 6.f);
		}
	}

	FVector Aim = FVector::ZeroVector;
	if (Best)
	{
		if (Target.Get() != Best || Now - LastSeenTime > 0.6f) Reaction = 0.45f;
		Target = Best;
		LastSeen = Best->ChestLocation();
		LastSeenTime = Now;
		State = ESLBotState::Engage;
		Aim = LastSeen;
	}
	else if (Now < SuppressUntil)
	{
		State = ESLBotState::Suppress;
		Aim = LastSeen;
	}
	else
	{
		State = ESLBotState::Watch;
		Target = nullptr;
	}

	// turn
	const float WantYaw = State == ESLBotState::Watch ? HomeYaw + 18.f * FMath::Sin(Now * 0.35f + Index) : (Aim - EyeLocation()).Rotation().Yaw;
	FRotator R = GetActorRotation();
	R.Yaw = FMath::FixedTurn(R.Yaw, WantYaw, 260.f * Dt);
	SetActorRotation(R);

	// shoot
	Reaction -= Dt;
	ShotTimer -= Dt;
	if (!bFire) return;
	if (State == ESLBotState::Engage && Reaction <= 0.f)
	{
		if (BurstShots <= 0 && ShotTimer <= 0.f) { BurstShots = 3; ShotTimer = 0.f; }
		if (BurstShots > 0 && ShotTimer <= 0.f)
		{
			const float Moving = Best ? FMath::Clamp(Best->GetVelocity().Size2D() / 430.f, 0.f, 1.f) : 0.f;
			Shoot(Aim, 0.42f - 0.22f * Moving);
			--BurstShots;
			ShotTimer = BurstShots > 0 ? 0.11f : 0.85f;
		}
	}
	else if (State == ESLBotState::Suppress)
	{
		if (BurstShots <= 0 && ShotTimer <= 0.f) { BurstShots = 5; ShotTimer = 0.f; }
		if (BurstShots > 0 && ShotTimer <= 0.f)
		{
			Shoot(LastSeen + FMath::VRand() * 70.f, 0.12f);
			--BurstShots;
			ShotTimer = BurstShots > 0 ? 0.1f : 2.0f;
		}
		// the platform sentry clears the smoke with an HE grenade
		HECooldown -= Dt;
		if (bHE && HELeft > 0 && HECooldown <= 0.f && M && M->DensityAt(LastSeen, false) > 0.2f)
		{
			ThrowHE(LastSeen);
			HECooldown = 12.f;
		}
	}
	else
	{
		HECooldown = FMath::Max(HECooldown - Dt, 0.f);
	}
}

void ASLBot::Shoot(const FVector& AimAt, float HitChance)
{
	const FVector Start = EyeLocation() + GetActorForwardVector() * 30.f;
	FVector Dir = (AimAt - Start).GetSafeNormal();
	const bool bHitRoll = FMath::FRand() < HitChance;
	if (!bHitRoll) Dir = FMath::VRandCone(Dir, FMath::DegreesToRadians(3.5f));
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SLBotFire), false, this);
	const FVector Far = Start + Dir * 9000.f;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, Far, ECC_Visibility, Q);
	const FVector End = bHit ? FVector(Hit.ImpactPoint) : Far;
	if (ASLSmokeManager* M = ASLSmokeManager::Get(this)) M->AddBullet(Start, End, 1.f, -100 - Index);
	MulticastShot(Start, End, -100 - Index);
	if (bHit && bHitRoll)
		if (ASLCharacter* C = Cast<ASLCharacter>(Hit.GetActor()))
			C->TakeDamage(11.f, FDamageEvent(), nullptr, this);
}

void ASLBot::ThrowHE(const FVector& At)
{
	--HELeft;
	const FVector Start = EyeLocation() + GetActorForwardVector() * 40.f;
	// lob: aim a bit above the target, flight time ~1 s
	const FVector D = At - Start;
	const float T = FMath::Clamp(D.Size2D() / 1100.f, 0.5f, 1.4f);
	const FVector V(D.X / T, D.Y / T, (D.Z + 0.5f * 980.f * T * T) / T);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.Instigator = this;
	if (ASLGrenade* G = GetWorld()->SpawnActor<ASLGrenade>(Start, V.Rotation(), P)) G->Launch(ESLGrenade::HE, V, this);
	if (ASLGameState* GS = ASLGameState::Get(this)) GS->AddFeed(FString::Printf(TEXT("哨兵 %d 向烟雾投出高爆手雷"), Index + 1));
}

void ASLBot::OnDied()
{
	State = ESLBotState::Dead;
	if (ASLGameState* GS = ASLGameState::Get(this))
	{
		GS->BotsAlive = 0;
		for (TActorIterator<ASLBot> It(GetWorld()); It; ++It) if (!It->IsDead()) ++GS->BotsAlive;
		GS->AddFeed(FString::Printf(TEXT("哨兵 %d 被击倒%s"), Index + 1, bThroughSmokeLastHit ? TEXT("（穿烟击杀）") : TEXT("")));
	}
}

bool ASLBot::SmokeHides(const FVector& Eye) const
{
	const ASLSmokeManager* M = ASLSmokeManager::Get(this);
	if (!M) return false;
	// optimistic: hidden only if every probe (head, chest, feet, and the same after 0.25 s of motion) sits behind
	// enough smoke; geometry is left to the engine's normal rules
	const FVector Ahead = GetVelocity() * 0.25f;
	const FVector Probes[6] = { EyeLocation(), ChestLocation(), GetActorLocation() - FVector(0, 0, 70),
		EyeLocation() + Ahead, ChestLocation() + Ahead, GetActorLocation() - FVector(0, 0, 70) + Ahead };
	for (const FVector& P : Probes)
		if (M->SmokeLength(Eye, P) < SL::BotSmokeBlockCm * 0.5f) return false;
	return true;
}

bool ASLBot::IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const
{
	const ASLSmokeManager* M = ASLSmokeManager::Get(this);
	if (!M || !M->Settings.bRelevancyCull || IsDead()) return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
	const APlayerController* PC = Cast<APlayerController>(RealViewer);
	if (!PC || PC->IsLocalController()) return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bSmokeHidden = SmokeHides(SrcLocation);
	// hysteresis: once visible, stay sent for 0.5 s
	double& Until = RelevantUntil.FindOrAdd(RealViewer);
	if (!bSmokeHidden) Until = Now + 0.5;
	if (Now < Until) return Super::IsNetRelevantFor(RealViewer, ViewTarget, SrcLocation);
	return false;
}
