#include "SLCharacter.h"

#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "SLBot.h"
#include "SLGame.h"
#include "SLGrenade.h"
#include "SLLevel.h"
#include "SLSmoke.h"
#include "SLSmokeManager.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	template <class T> T* Find(const TCHAR* Path) { return LoadObject<T>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet); }
}

// ---------------------------------------------------------------- soldier

ASLSoldier::ASLSoldier()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);
	// soldiers never block smoke voxelisation or blast line checks
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	// hitscan bullets use the visibility channel: soldiers must stop them
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Manny(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (Manny.Succeeded()) GetMesh()->SetSkeletalMesh(Manny.Object);
	GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -92.f), FRotator(0, -90.f, 0));
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	GetMesh()->SetCollisionProfileName(TEXT("NoCollision"));
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Rifle3P = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rifle3P"));
	Rifle3P->SetupAttachment(GetMesh(), TEXT("hand_r"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Rifle(TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle"));
	if (Rifle.Succeeded()) Rifle3P->SetStaticMesh(Rifle.Object);
	Rifle3P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rifle3P->SetRelativeLocationAndRotation(FVector(-10.f, 4.f, -1.f), FRotator(0.f, 90.f, 0.f));
	GetCharacterMovement()->MaxWalkSpeed = 430.f;
	GetCharacterMovement()->MaxWalkSpeedCrouched = 200.f;
	GetCharacterMovement()->NavAgentProps.bCanCrouch = true;
	GetCharacterMovement()->JumpZVelocity = 420.f;
}

void ASLSoldier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASLSoldier, Health);
	DOREPLIFETIME(ASLSoldier, Kills);
}

void ASLSoldier::BeginPlay()
{
	Super::BeginPlay();
	AnimIdle = Find<UAnimSequence>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS"));
	AnimJog = Find<UAnimSequence>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/Jog/MF_Rifle_Jog_Fwd.MF_Rifle_Jog_Fwd"));
	AnimFire = Find<UAnimSequence>(TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Fire.MM_Rifle_Fire"));
	for (const TCHAR* P : { TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01.MM_Death_Front_01"),
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_02.MM_Death_Front_02"),
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Back_01.MM_Death_Back_01") })
		if (UAnimSequence* S = Find<UAnimSequence>(P)) AnimDeath.Add(S);
	SndShot = Find<USoundBase>(TEXT("/Game/SmokeLab/Audio/S_RifleShot.S_RifleShot"));
	if (!SndShot) SndShot = Find<USoundBase>(TEXT("/Game/Weapons/GrenadeLauncher/Audio/FirstPersonTemplateWeaponFire02.FirstPersonTemplateWeaponFire02"));
	SndShotFar = Find<USoundBase>(TEXT("/Game/SmokeLab/Audio/S_RifleShotFar.S_RifleShotFar"));
	PlayLoop(AnimIdle);
}

void ASLSoldier::PlayLoop(UAnimSequence* Seq)
{
	if (!Seq || Current.Get() == Seq || OnceTimer > 0.f || bDeathPlayed) return;
	GetMesh()->PlayAnimation(Seq, true);
	Current = Seq;
}

void ASLSoldier::PlayOnce(UAnimSequence* Seq, float Hold)
{
	if (!Seq || bDeathPlayed) return;
	GetMesh()->PlayAnimation(Seq, false);
	Current = Seq;
	OnceTimer = Hold > 0.f ? Hold : Seq->GetPlayLength();
}

void ASLSoldier::Tick(float Dt)
{
	Super::Tick(Dt);
	if (OnceTimer > 0.f)
	{
		OnceTimer -= Dt;
		if (OnceTimer <= 0.f && !bDeathPlayed) Current = nullptr;
	}
	if (!bDeathPlayed && OnceTimer <= 0.f)
		PlayLoop(GetVelocity().Size2D() > 120.f && AnimJog ? AnimJog.Get() : AnimIdle.Get());
}

FVector ASLSoldier::EyeLocation() const
{
	return GetActorLocation() + FVector(0, 0, bIsCrouched ? 40.f : BaseEyeHeight);
}

FVector ASLSoldier::MuzzleLocation() const
{
	return Rifle3P ? Rifle3P->GetComponentTransform().TransformPosition(FVector(0, 55, 10)) : EyeLocation();
}

float ASLSoldier::TakeDamage(float Damage, FDamageEvent const& Event, AController* Inst, AActor* Causer)
{
	if (!HasAuthority() || IsDead()) return 0.f;
	Health = FMath::Max(0.f, Health - Damage);
	LastHitTime = GetWorld()->GetTimeSeconds();
	if (IsDead())
	{
		bool bSmoke = false;
		if (ASLSmokeManager* M = ASLSmokeManager::Get(this))
			if (Causer) bSmoke = M->SmokeLength(Causer->GetActorLocation() + FVector(0, 0, 60), ChestLocation(), false) > 60.f;
		if (ASLSoldier* K = Cast<ASLSoldier>(Causer)) ++K->Kills;
		MulticastDied(bSmoke);
		OnDied();
	}
	OnRep_Health();
	return Damage;
}

void ASLSoldier::OnRep_Health()
{
	if (IsDead() && !bDeathPlayed && AnimDeath.Num())
	{
		PlayOnce(AnimDeath[FMath::RandRange(0, AnimDeath.Num() - 1)], 1000.f);
		GetMesh()->bPauseAnims = false;
		bDeathPlayed = true;
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	else if (!IsDead() && bDeathPlayed)
	{
		bDeathPlayed = false;
		OnceTimer = 0.f;
		Current = nullptr;
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		PlayLoop(AnimIdle);
	}
}

void ASLSoldier::MulticastDied_Implementation(bool bThroughSmoke)
{
	bThroughSmokeLastHit = bThroughSmoke;
}

void ASLSoldier::PlayFireFx(const FVector& End)
{
	ASLFx::Tracer(GetWorld(), MuzzleLocation(), End, IsA<ASLBot>());
	// my own rifle: the close-up recording, unspatialised; everyone else's: the mid-distance take with its echo, in 3D
	const float Pitch = FMath::FRandRange(0.96f, 1.04f);
	if (IsLocallyControlled() && !IsA<ASLBot>() && SndShot) UGameplayStatics::PlaySound2D(this, SndShot, 0.5f, Pitch);
	else if (USoundBase* S = SndShotFar ? SndShotFar.Get() : SndShot.Get()) UGameplayStatics::PlaySoundAtLocation(this, S, MuzzleLocation(), 0.8f, Pitch);
	if (AnimFire && OnceTimer <= 0.f) PlayOnce(AnimFire, 0.18f);
}

void ASLSoldier::MulticastShot_Implementation(FVector_NetQuantize Start, FVector_NetQuantize End, int32 Shooter)
{
	// the shooter already showed its own predicted shot
	if (IsLocallyControlled() && !IsA<ASLBot>()) return;
	PlayFireFx(End);
}

// ---------------------------------------------------------------- player

ASLCharacter::ASLCharacter()
{
	BaseEyeHeight = 64.f;
	CrouchedEyeHeight = 30.f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0, 0, 64.f));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(90.f);
	Rifle1P = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rifle1P"));
	Rifle1P->SetupAttachment(Camera);
	Rifle1P->SetRelativeLocationAndRotation(FVector(38.f, 15.f, -19.f), FRotator(0.f, -90.f, 0.f));
	Rifle1P->SetRelativeScale3D(FVector(0.62f));
	Rifle1P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rifle1P->SetOnlyOwnerSee(true);
	Rifle1P->SetCastShadow(false);
	Rifle1P->bUseAsOccluder = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Rifle(TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle"));
	if (Rifle.Succeeded()) Rifle1P->SetStaticMesh(Rifle.Object);
	GetMesh()->SetOwnerNoSee(true);
	Rifle3P->SetOwnerNoSee(true);
	bUseControllerRotationYaw = true;
}

void ASLCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ASLCharacter, Ammo, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASLCharacter, Reserve, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASLCharacter, Smokes, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASLCharacter, HEs, COND_OwnerOnly);
	DOREPLIFETIME(ASLCharacter, PlantProgress);
	DOREPLIFETIME(ASLCharacter, bInPlantZone);
	DOREPLIFETIME_CONDITION(ASLCharacter, bSeenByBot, COND_OwnerOnly);
}

void ASLCharacter::BeginPlay()
{
	Super::BeginPlay();
	// The template rifle is light grey; a dark gunmetal reads better against bright smoke.
	if (UMaterialInterface* Gun = Find<UMaterialInterface>(TEXT("/Game/SmokeLab/Materials/M_Gun.M_Gun")))
		for (UStaticMeshComponent* C : { Rifle1P.Get(), Rifle3P.Get() })
			if (C) for (int32 I = 0; I < C->GetNumMaterials(); ++I) C->SetMaterial(I, Gun);
}

void ASLCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Super::CalcCamera(DeltaTime, OutResult);
	// recoil kick, decays quickly
	OutResult.Rotation.Pitch += Recoil * 0.6f;
}

float ASLCharacter::Spread() const
{
	const float Move = FMath::Clamp(GetVelocity().Size2D() / 430.f, 0.f, 1.f);
	return 0.35f + Move * 2.4f + Recoil * 0.35f + (GetCharacterMovement()->IsFalling() ? 4.f : 0.f);
}

void ASLCharacter::Tick(float Dt)
{
	Super::Tick(Dt);
	Recoil = FMath::FInterpTo(Recoil, 0.f, Dt, 6.f);
	FireCooldown -= Dt;
	if (ReloadLeft > 0.f)
	{
		ReloadLeft -= Dt;
		if (ReloadLeft <= 0.f && HasAuthority())
		{
			const int32 Need = FMath::Min(30 - Ammo, Reserve);
			Ammo += Need; Reserve -= Need;
		}
	}
	if (IsLocallyControlled() && bWantsFire && FireCooldown <= 0.f && ReloadLeft <= 0.f && !IsDead())
	{
		if (Ammo > 0) FireOnce();
		else Reload();
	}
	if (HasAuthority())
	{
		ASLLevel* L = ASLLevel::Get(this);
		bInPlantZone = L && L->Spots().PlantZone.IsInside(GetActorLocation()) && !IsDead();
		ASLGameState* GS = ASLGameState::Get(this);
		if (bUseHeld && bInPlantZone && GS && GS->Round == ESLRound::Playing && GetVelocity().Size2D() < 20.f)
		{
			PlantProgress += Dt / 3.f;
			if (PlantProgress >= 1.f)
			{
				PlantProgress = 1.f;
				if (ASLGameMode* GM = GetWorld()->GetAuthGameMode<ASLGameMode>()) GM->OnPlanted(this);
			}
		}
		else if (PlantProgress < 1.f) PlantProgress = 0.f;
		// am I seen by any sentry? (shown on the HUD: the server decides, the player only reads it)
		bool bSeen = false;
		for (TActorIterator<ASLBot> It(GetWorld()); It; ++It)
		{
			float Cm; bool bClear;
			if (!It->IsDead() && It->CanSee(this, Cm, bClear)) { bSeen = true; break; }
		}
		bSeenByBot = bSeen && !IsDead();
	}
}

FVector ASLCharacter::MuzzleLocation() const
{
	// my own view shows the first-person rifle; everyone else sees the third-person one in the hand
	if (IsLocallyControlled() && Rifle1P) return Rifle1P->GetComponentTransform().TransformPosition(FVector(0, 55, 10));
	return Super::MuzzleLocation();
}

void ASLCharacter::FireOnce()
{
	// Usual FPS split: the camera ray (with spread) decides what the crosshair is on, the bullet itself leaves the
	// muzzle towards that point. A wall between the muzzle and the aim point stops it there.
	FireCooldown = 0.1f;
	const FRotator Aim = GetControlRotation();
	const FVector Eye = Camera->GetComponentLocation();
	FRandomStream R(FMath::Rand());
	const FVector AimDir = R.VRandCone(Aim.Vector(), FMath::DegreesToRadians(Spread()));
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SLFire), false, this);
	const FVector AimPoint = GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + AimDir * 12000.f, ECC_Visibility, Q) ? FVector(Hit.ImpactPoint) : Eye + AimDir * 12000.f;
	const FVector Start = MuzzleLocation();
	const FVector Dir = (AimPoint - Start).GetSafeNormal();
	const FVector Far = Start + Dir * 12000.f;
	const FVector End = GetWorld()->LineTraceSingleByChannel(Hit, Start, Far, ECC_Visibility, Q) ? Hit.ImpactPoint : Far;
	// local presentation and the locally predicted tunnel (the server's copy replaces it)
	PlayFireFx(End);
	if (ASLSmokeManager* M = ASLSmokeManager::Get(this))
		if (M->Settings.bBullets) M->AddLocalBullet(Start, End, 1.f, GetPlayerState() ? GetPlayerState()->GetPlayerId() : -1);
	Recoil = FMath::Min(Recoil + 0.9f, 8.f);
	LastShotLocal = GetWorld()->GetTimeSeconds();
	if (!HasAuthority()) --Ammo;
	ServerFire(Start, Dir);
}

void ASLCharacter::ServerFire_Implementation(FVector_NetQuantize InStart, FVector_NetQuantizeNormal Dir)
{
	if (IsDead() || Ammo <= 0) return;
	// the client's first-person muzzle; accept it only near the eye
	const FVector Eye = EyeLocation();
	const FVector Start = FVector::Dist(InStart, Eye) < 150.f ? FVector(InStart) : Eye;
	--Ammo;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SLFire), false, this);
	const FVector Far = Start + Dir * 12000.f;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, Far, ECC_Visibility, Q);
	const FVector End = bHit ? FVector(Hit.ImpactPoint) : Far;
	const int32 Id = GetPlayerState() ? GetPlayerState()->GetPlayerId() : -1;
	if (ASLSmokeManager* M = ASLSmokeManager::Get(this)) M->AddBullet(Start, End, 1.f, Id);
	MulticastShot(Start, End, Id);
	if (bHit)
		if (ASLBot* B = Cast<ASLBot>(Hit.GetActor()))
		{
			if (!B->IsDead())
			{
				const bool bHead = Hit.ImpactPoint.Z > B->GetActorLocation().Z + 60.f;
				B->TakeDamage(bHead ? 100.f : 34.f, FDamageEvent(), GetController(), this);
				ClientHitMarker(B->IsDead());
			}
		}
}

void ASLCharacter::ClientHitMarker_Implementation(bool bKill)
{
	++HitMarkers;
	HitMarkerTime = GetWorld()->GetTimeSeconds();
}

void ASLCharacter::Reload()
{
	if (ReloadLeft > 0.f || Ammo >= 30 || Reserve <= 0) return;
	ReloadLeft = 2.0f;
	ServerReload();
}

void ASLCharacter::ServerReload_Implementation()
{
	if (ReloadLeft <= 0.f && Ammo < 30 && Reserve > 0) ReloadLeft = 2.0f;
}

void ASLCharacter::Throw(ESLGrenade Type)
{
	if (IsDead()) return;
	const FRotator Aim = GetControlRotation();
	const FVector Start = Camera->GetComponentLocation() + Aim.Vector() * 40.f + FVector(0, 0, -10.f);
	const FVector Vel = Aim.Vector() * 1150.f + FVector(0, 0, 180.f) + GetVelocity() * 0.5f;
	ServerThrow(Type, Start, Vel);
}

void ASLCharacter::ServerThrow_Implementation(ESLGrenade Type, FVector_NetQuantize Start, FVector_NetQuantize Velocity)
{
	if (IsDead()) return;
	int32& Count = Type == ESLGrenade::Smoke ? Smokes : HEs;
	if (Count <= 0) return;
	--Count;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.Instigator = this;
	if (ASLGrenade* G = GetWorld()->SpawnActor<ASLGrenade>(Start, Velocity.Rotation(), P)) G->Launch(Type, Velocity, this);
}

void ASLCharacter::Use(bool bDown)
{
	bUseHeld = bDown;
	ServerUse(bDown);
}

void ASLCharacter::ServerUse_Implementation(bool bDown)
{
	bUseHeld = bDown;
	if (!bDown || IsDead()) return;
	// doors within reach
	for (TActorIterator<ASLDoor> It(GetWorld()); It; ++It)
		if (FVector::Dist2D(It->GetActorLocation() + FVector(0, 100, 0), GetActorLocation()) < 260.f || FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 240.f)
		{
			It->ServerToggle();
			return;
		}
}

void ASLCharacter::OnDied()
{
	bWantsFire = false;
	if (ASLGameMode* GM = GetWorld()->GetAuthGameMode<ASLGameMode>()) GM->OnPlayerDied(this);
}
