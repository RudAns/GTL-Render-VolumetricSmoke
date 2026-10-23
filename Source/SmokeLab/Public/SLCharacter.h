#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SLCharacter.generated.h"

UENUM()
enum class ESLGrenade : uint8 { Smoke, HE };

/** Shared look for soldiers: mannequin with a rifle, simple sequence-switching animation. */
UCLASS(Abstract)
class SMOKELAB_API ASLSoldier : public ACharacter
{
	GENERATED_BODY()
public:
	ASLSoldier();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float Dt) override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& Event, AController* Instigator, AActor* Causer) override;

	bool IsDead() const { return Health <= 0.f; }
	FVector EyeLocation() const;
	FVector ChestLocation() const { return GetActorLocation() + FVector(0, 0, 30); }
	/** Muzzle of the third-person rifle (world). */
	FVector MuzzleLocation() const;
	void PlayFireFx(const FVector& End);	// tracer + sound on this machine

	UPROPERTY(ReplicatedUsing = OnRep_Health) float Health = 100.f;
	UPROPERTY(Replicated) int32 Kills = 0;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Rifle3P;
	float LastHitTime = -10.f;
	bool bThroughSmokeLastHit = false;

	UFUNCTION(NetMulticast, Unreliable) void MulticastShot(FVector_NetQuantize Start, FVector_NetQuantize End, int32 Shooter);
	UFUNCTION(NetMulticast, Reliable) void MulticastDied(bool bThroughSmoke);

	UFUNCTION() virtual void OnRep_Health();
protected:
	virtual void OnDied() {}
	void PlayLoop(class UAnimSequence* Seq);
	void PlayOnce(class UAnimSequence* Seq, float Hold = 0.f);
	UPROPERTY() TObjectPtr<class UAnimSequence> AnimIdle;
	UPROPERTY() TObjectPtr<class UAnimSequence> AnimJog;
	UPROPERTY() TObjectPtr<class UAnimSequence> AnimFire;
	UPROPERTY() TArray<TObjectPtr<class UAnimSequence>> AnimDeath;
	UPROPERTY() TObjectPtr<class USoundBase> SndShot;
	UPROPERTY() TObjectPtr<class USoundBase> SndShotFar;
	UPROPERTY() TObjectPtr<class UMaterialInterface> TracerMat;
	TWeakObjectPtr<class UAnimSequence> Current;
	float OnceTimer = 0.f;
	bool bDeathPlayed = false;
};

/** The player: first person, rifle, smoke and HE grenades, plants the bomb. */
UCLASS()
class SMOKELAB_API ASLCharacter : public ASLSoldier
{
	GENERATED_BODY()
public:
	ASLCharacter();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void Tick(float Dt) override;
	virtual void BeginPlay() override;
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

	// input (called by the controller)
	void SetFiring(bool b) { bWantsFire = b; }
	void Throw(ESLGrenade Type);
	void Reload();
	void Use(bool bDown);

	UPROPERTY(Replicated) int32 Ammo = 30;
	UPROPERTY(Replicated) int32 Reserve = 120;
	UPROPERTY(Replicated) int32 Smokes = 3;
	UPROPERTY(Replicated) int32 HEs = 2;
	UPROPERTY(Replicated) float PlantProgress = 0.f;
	UPROPERTY(Replicated) bool bInPlantZone = false;
	UPROPERTY(Replicated) bool bSeenByBot = false;	// server's verdict: is any sentry seeing me
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Rifle1P;
	float ReloadLeft = 0.f;
	float Recoil = 0.f;
	float LastShotLocal = -10.f;
	bool bUseHeld = false;
	int32 HitMarkers = 0;
	float HitMarkerTime = -10.f;

	UFUNCTION(Server, Unreliable) void ServerFire(FVector_NetQuantize Start, FVector_NetQuantizeNormal Dir);
	UFUNCTION(Server, Reliable) void ServerThrow(ESLGrenade Type, FVector_NetQuantize Start, FVector_NetQuantize Velocity);
	UFUNCTION(Server, Reliable) void ServerReload();
	UFUNCTION(Server, Reliable) void ServerUse(bool bDown);
	UFUNCTION(Client, Unreliable) void ClientHitMarker(bool bKill);

protected:
	virtual void OnDied() override;
private:
	bool bWantsFire = false;
	float FireCooldown = 0.f;
	float Spread() const;
	void FireOnce();
};
