#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SLTypes.h"
#include "SLVoxel.h"
#include "SLSmoke.generated.h"

/**
 * One smoke cloud. Replicated: the inputs (detonation point, rule, start time) and, in the
 * server-result sync mode, the compressed arrival field the server computed.
 */
UCLASS()
class SMOKELAB_API ASLSmoke : public AActor
{
	GENERATED_BODY()
public:
	ASLSmoke();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float Dt) override;

	/** Server: set inputs and fill. */
	void ServerInit(const FVector& Where, ESLSpreadRule InRule, ESLSyncMode InSync, float Now, uint8 InBit, bool bSlow);
	/** Server: the level changed (door): fill again, keep the start time. */
	void ServerRefill();
	/** Client (local-fill mode) or server: (re)compute from this machine's own voxel world. */
	void FillLocally();

	// --- queries (any machine) ---
	bool HasField() const { return Field.bOk && Field.Arrival.Num() == SL::N3; }
	float Age(float Now) const { return Now - StartTime; }
	float Radius(float Now) const;		// visible arrival radius, voxels
	float DensityScale(float Now) const;	// fade-out multiplier
	/** Base density 0..1 at P (growth and edge feather, no carving, no noise). */
	float Density(const FVector& P, float Now) const;
	FBox Bounds() const;
	FVector GridOriginWorld() const;

	UPROPERTY(ReplicatedUsing = OnRep_Inputs) FVector_NetQuantize Detonation;
	UPROPERTY(ReplicatedUsing = OnRep_Inputs) ESLSpreadRule Rule = ESLSpreadRule::Volume;
	UPROPERTY(ReplicatedUsing = OnRep_Inputs) ESLSyncMode Sync = ESLSyncMode::ServerResult;
	UPROPERTY(ReplicatedUsing = OnRep_Inputs) uint16 Version = 0;
	UPROPERTY(Replicated) float StartTime = 0.f;
	UPROPERTY(Replicated) uint8 Bit = 0;
	UPROPERTY(Replicated) bool bSlowGrowth = false;
	UPROPERTY(Replicated) FIntVector GridOrigin = FIntVector::ZeroValue;
	UPROPERTY(ReplicatedUsing = OnRep_Packed) TArray<uint8> Packed;

	FSLFillResult Field;
	bool bFieldFromServer = false;
	uint32 FieldCrc = 0;
	int32 ReplicatedBytes = 0;	// bytes this smoke cost on the wire
	int32 DirtyCount = 0;		// bumped whenever the field changes (renderer re-uploads)

	UPROPERTY(Replicated) uint32 ServerFieldCrc = 0;

private:
	UFUNCTION() void OnRep_Inputs();
	UFUNCTION() void OnRep_Packed();
	void SetField(FSLFillResult&& R, bool bFromServer);
	uint16 SeenVersion = 0xffff;
};

/** A door the player can open: the smoke voxels around it are rebuilt and the clouds touching it refilled. */
UCLASS()
class SMOKELAB_API ASLDoor : public AActor
{
	GENERATED_BODY()
public:
	ASLDoor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void Tick(float Dt) override;
	void Setup(const FVector& HingeLoc, float Width, float Height, float ClosedYaw, class UMaterialInterface* Mat);
	void ServerToggle();
	FBox Region() const;
	UPROPERTY(ReplicatedUsing = OnRep_Open) bool bOpen = false;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Hinge;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Leaf;
	float Width = 150.f, Height = 260.f, ClosedYaw = 0.f, Angle = 0.f;
private:
	UFUNCTION() void OnRep_Open();
	bool bPendingRebuild = false;
	float SettleTime = 0.f;
};
