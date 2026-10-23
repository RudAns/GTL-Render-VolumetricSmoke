#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SLCharacter.h"
#include "SLGrenade.generated.h"

/** Thrown grenade. Smoke pops once it has come to rest (CS2 has a minimum fuse too); HE explodes on a timer. */
UCLASS()
class SMOKELAB_API ASLGrenade : public AActor
{
	GENERATED_BODY()
public:
	ASLGrenade();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void Tick(float Dt) override;
	void Launch(ESLGrenade InType, const FVector& Velocity, AActor* InThrower);

	UPROPERTY(Replicated) ESLGrenade Type = ESLGrenade::Smoke;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UProjectileMovementComponent> Move;
	UFUNCTION(NetMulticast, Reliable) void MulticastPop(FVector_NetQuantize Where, ESLGrenade What);
	UFUNCTION() void OnBounce(const FHitResult& Hit, const FVector& Velocity);
private:
	float Age = 0.f;
	float RestTime = 0.f;
	bool bPopped = false;
	TWeakObjectPtr<AActor> Thrower;
	float SmokeEndTime = 0.f;
	void Detonate();
};

/** Local-only effects: tracer streak, explosion flash. */
UCLASS()
class SMOKELAB_API ASLFx : public AActor
{
	GENERATED_BODY()
public:
	ASLFx();
	virtual void Tick(float Dt) override;
	static void Tracer(UWorld* W, const FVector& A, const FVector& B, bool bBot);
	static void Blast(UWorld* W, const FVector& At);
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UPointLightComponent> Light;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> Mid;
	float Life = 0.1f, Age = 0.f, BaseIntensity = 0.f;
	// a moving tracer: a streak that leaves the muzzle and flies to the impact
	FVector From = FVector::ZeroVector, Dir = FVector::ForwardVector;
	float Length = 0.f, Streak = 0.f, Speed = 0.f;
	void PlaceStreak();
};
