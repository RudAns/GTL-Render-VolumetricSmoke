#include "SLGrenade.h"

#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "SLBot.h"
#include "SLGame.h"
#include "SLSmokeManager.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	template <class T> T* Find(const TCHAR* Path) { return LoadObject<T>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet); }
	void Sound(const UObject* Ctx, const TCHAR* Path, const FVector& At, float Vol = 1.f)
	{
		if (USoundBase* S = Find<USoundBase>(Path)) UGameplayStatics::PlaySoundAtLocation(Ctx, S, At, Vol, FMath::FRandRange(0.95f, 1.05f));
	}
}

ASLGrenade::ASLGrenade()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(5.f);
	Sphere->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	RootComponent = Sphere;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded()) Mesh->SetStaticMesh(Cyl.Object);
	Mesh->SetRelativeScale3D(FVector(0.07f, 0.07f, 0.13f));
	Move = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Move"));
	Move->bShouldBounce = true;
	Move->Bounciness = 0.32f;
	Move->Friction = 0.55f;
	Move->InitialSpeed = 0.f;
	Move->MaxSpeed = 3000.f;
	Move->ProjectileGravityScale = 1.f;
	Move->bRotationFollowsVelocity = false;
	Move->OnProjectileBounce.AddDynamic(this, &ASLGrenade::OnBounce);
}

void ASLGrenade::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASLGrenade, Type);
}

void ASLGrenade::Launch(ESLGrenade InType, const FVector& Velocity, AActor* InThrower)
{
	Type = InType;
	Thrower = InThrower;
	Move->Velocity = Velocity;
	if (UMaterialInterface* M = Find<UMaterialInterface>(Type == ESLGrenade::Smoke ? TEXT("/Game/SmokeLab/Materials/M_Metal.M_Metal") : TEXT("/Game/SmokeLab/Materials/M_Grenade.M_Grenade")))
		Mesh->SetMaterial(0, M);
	if (InThrower && InThrower->IsA<ASLBot>()) Sphere->IgnoreActorWhenMoving(InThrower, true);
}

void ASLGrenade::OnBounce(const FHitResult& Hit, const FVector& Velocity)
{
	if (Velocity.Size() > 150.f) Sound(this, TEXT("/Game/SmokeLab/Audio/S_Bounce.S_Bounce"), GetActorLocation(), FMath::Clamp(Velocity.Size() / 900.f, 0.2f, 1.f));
}

void ASLGrenade::Tick(float Dt)
{
	Super::Tick(Dt);
	Age += Dt;
	if (!HasAuthority()) return;
	if (bPopped)
	{
		if (Type == ESLGrenade::Smoke && Age > SmokeEndTime) Destroy();
		return;
	}
	if (Type == ESLGrenade::Smoke)
	{
		// CS2: a minimum fuse, then it pops once it has (nearly) stopped
		RestTime = Move->Velocity.Size() < 40.f ? RestTime + Dt : 0.f;
		if ((Age > 1.0f && RestTime > 0.2f) || Age > 4.f) Detonate();
	}
	else if (Age > 1.6f) Detonate();
}

void ASLGrenade::Detonate()
{
	bPopped = true;
	const FVector At = GetActorLocation();
	ASLSmokeManager* M = ASLSmokeManager::Get(this);
	if (Type == ESLGrenade::Smoke)
	{
		if (M) M->SpawnSmoke(At);
		Move->StopMovementImmediately();
		Move->ProjectileGravityScale = 0.f;
		SmokeEndTime = Age + SL::LifeSeconds;
	}
	else
	{
		if (M) M->AddBlast(At);
		// damage soldiers in the open within 3.8 m
		for (TActorIterator<ASLSoldier> It(GetWorld()); It; ++It)
		{
			const float D = FVector::Dist(It->ChestLocation(), At);
			if (D > 380.f || It->IsDead()) continue;
			FHitResult H;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(SLHE), false, this);
			Q.AddIgnoredActor(*It);
			if (GetWorld()->LineTraceSingleByChannel(H, At + FVector(0, 0, 20), It->ChestLocation(), ECC_GameTraceChannel1, Q)) continue;
			It->TakeDamage(95.f * (1.f - D / 380.f), FDamageEvent(), nullptr, Thrower.Get() ? Thrower.Get() : this);
		}
		SetLifeSpan(0.6f);
		Mesh->SetVisibility(false);
		Move->StopMovementImmediately();
	}
	MulticastPop(At, Type);
}

void ASLGrenade::MulticastPop_Implementation(FVector_NetQuantize Where, ESLGrenade What)
{
	if (What == ESLGrenade::Smoke) Sound(this, TEXT("/Game/SmokeLab/Audio/S_SmokePop.S_SmokePop"), Where, 0.9f);
	else
	{
		Sound(this, TEXT("/Game/SmokeLab/Audio/S_Explosion.S_Explosion"), Where, 1.f);
		ASLFx::Blast(GetWorld(), Where);
	}
}

// ---------------------------------------------------------------- fx

ASLFx::ASLFx()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded()) Mesh->SetStaticMesh(Cyl.Object);
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Mesh);
	Light->SetIntensity(0.f);
	Light->SetCastShadows(false);
	Light->SetVisibility(false);
}

void ASLFx::Tracer(UWorld* W, const FVector& A, const FVector& B, bool bBot)
{
	if (!W || W->GetNetMode() == NM_DedicatedServer) return;
	const FVector D = B - A;
	const float L = D.Size();
	if (L < 10.f) return;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// the streak leaves the muzzle and flies to the impact at tracer speed (much slower than a real round, so the
	// eye can follow it), with a brief muzzle flash
	ASLFx* F = W->SpawnActor<ASLFx>(A, FRotationMatrix::MakeFromZ(D).Rotator(), P);
	if (!F) return;
	if (UMaterialInterface* M = Find<UMaterialInterface>(TEXT("/Game/SmokeLab/Materials/M_Tracer.M_Tracer"))) F->Mesh->SetMaterial(0, M);
	F->From = A; F->Dir = D / L; F->Length = L;
	F->Streak = FMath::Min(L, 300.f);
	F->Speed = 15000.f;
	F->Life = FMath::Min((L + F->Streak) / F->Speed, 0.4f);
	F->PlaceStreak();
	F->Light->SetVisibility(true);
	F->Light->SetLightColor(FLinearColor(1.f, 0.75f, 0.4f));
	F->Light->SetAttenuationRadius(450.f);
	F->BaseIntensity = bBot ? 6000.f : 2500.f;
	F->Light->SetIntensity(F->BaseIntensity);
}

void ASLFx::PlaceStreak()
{
	const float Head = FMath::Min(Age * Speed + Streak, Length);
	const float Tail = FMath::Clamp(Head - Streak, 0.f, Length);
	const float Seg = FMath::Max(Head - Tail, 1.f);
	Mesh->SetWorldLocation(From + Dir * (Tail + Seg * 0.5f));
	Mesh->SetWorldScale3D(FVector(0.018f, 0.018f, Seg / 100.f));
	Mesh->SetVisibility(Head - Tail > 2.f);
	Light->SetWorldLocation(From);	// the flash stays at the muzzle while the streak flies on
}

void ASLFx::Blast(UWorld* W, const FVector& At)
{
	if (!W || W->GetNetMode() == NM_DedicatedServer) return;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASLFx* F = W->SpawnActor<ASLFx>(At + FVector(0, 0, 60), FRotator::ZeroRotator, P);
	if (!F) return;
	F->Mesh->SetVisibility(false);
	F->Light->SetVisibility(true);
	F->Light->SetLightColor(FLinearColor(1.f, 0.6f, 0.3f));
	F->Light->SetAttenuationRadius(1400.f);
	F->BaseIntensity = 400000.f;
	F->Light->SetIntensity(F->BaseIntensity);
	F->Life = 0.35f;
}

void ASLFx::Tick(float Dt)
{
	Super::Tick(Dt);
	Age += Dt;
	if (Speed > 0.f)
	{
		PlaceStreak();
		// the flash lasts 40 ms however long the tracer flies
		Light->SetIntensity(BaseIntensity * FMath::Square(FMath::Clamp(1.f - Age / 0.04f, 0.f, 1.f)));
	}
	else if (BaseIntensity > 0.f) Light->SetIntensity(BaseIntensity * FMath::Square(FMath::Clamp(1.f - Age / Life, 0.f, 1.f)));
	if (Age > Life) Destroy();
}
