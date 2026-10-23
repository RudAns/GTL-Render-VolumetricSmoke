#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SLRender.h"
#include "SLTypes.h"
#include "SLVoxel.h"
#include "SLSmokeManager.generated.h"

class ASLSmoke;
class ASLDoor;

/** Client-side view options (never replicated: each machine renders as it likes). */
struct FSLViewOptions
{
	FSLRenderSettings Render;
	bool bShowVoxels = false;		// draw the filled voxels of every smoke as cubes
	bool bShowLevelVoxels = false;	// draw the level's solid voxels around smokes
	bool bInjectDiff = false;		// this client's level copy treats the grate as solid (desync demo)
	bool bWallhack = false;			// draw every enemy this machine knows about through walls
	bool bShowSight = false;		// draw bot sight lines with the smoke integral
};

/**
 * Owns the voxelised level, every smoke, the bullet/blast event buffers and the renderer.
 * Spawned by the game mode on the server and replicated to clients.
 */
UCLASS()
class SMOKELAB_API ASLSmokeManager : public AActor
{
	GENERATED_BODY()
public:
	ASLSmokeManager();
	static ASLSmokeManager* Get(const UObject* Ctx);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float Dt) override;

	float Now() const;

	// --- server ---
	ASLSmoke* SpawnSmoke(const FVector& Where);
	void AddBullet(const FVector& Start, const FVector& End, float RadiusScale, int32 Shooter);
	void AddBlast(const FVector& Where);
	void OnDoorChanged(ASLDoor* Door);
	void ResetRound();

	// --- client prediction of the local player's own shots ---
	void AddLocalBullet(const FVector& Start, const FVector& End, float RadiusScale, int32 Shooter);

	// --- queries (gameplay density field: voxels + carving, no noise) ---
	float DensityAt(const FVector& P, bool bCarve = true) const;
	/** Centimetres of full-density smoke along the segment. */
	float SmokeLength(const FVector& A, const FVector& B, bool bCarve = true) const;
	bool IsInsideSmoke(const FVector& P, float Threshold = 0.35f) const { return DensityAt(P) > Threshold; }

	// --- level voxels ---
	bool EnsureWorld();
	const FSLVoxelWorld& VoxelWorld() const { return World; }
	FSLVoxelWorld& MutableVoxelWorld() { return World; }
	void RebuildRegion(const FBox& Region);

	const TArray<TWeakObjectPtr<ASLSmoke>>& GetSmokes() const { return Smokes; }
	void RegisterSmoke(ASLSmoke* S);
	void UnregisterSmoke(ASLSmoke* S);
	void SetInjectDiff(bool b);

	UPROPERTY(Replicated) FSLSharedSettings Settings;
	UPROPERTY(Replicated) TArray<FSLBulletEvent> Bullets;
	UPROPERTY(Replicated) TArray<FSLBlastEvent> Blasts;
	FSLViewOptions View;
	FSLGpuTimes GpuTimes;
	int32 LocalPlayerId = -2;
	double LastFillMicros = 0.0;
	int32 LastFillFilled = 0, LastFillLeaked = 0;
	int32 WorldBuildMs = 0;

private:
	FSLVoxelWorld World;
	bool bWorldTried = false;
	TArray<TWeakObjectPtr<ASLSmoke>> Smokes;
	TArray<FSLBulletEvent> LocalBullets;
	TSharedPtr<FSLSmokeRenderer, ESPMode::ThreadSafe> Renderer;
	TMap<int32, int32> UploadedDirty;	// slot -> smoke dirty count uploaded
	TMap<int32, TWeakObjectPtr<ASLSmoke>> SlotOwner;
	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> VoxelCubes;
	UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> LevelCubes;
	float VoxelVizTimer = 0.f;
	void FeedRenderer();
	void UpdateVoxelViz(float Dt);
	float BulletCarve(const FVector& P, const TArray<FSLBulletEvent>& List) const;
	float BlastFactor(const FVector& P, int32 Bit) const;
};
