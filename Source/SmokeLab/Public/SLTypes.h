#pragma once

#include "CoreMinimal.h"
#include "SLTypes.generated.h"

/** How a smoke fills space (report chapter 04). */
UENUM()
enum class ESLSpreadRule : uint8
{
	Ellipsoid,	// A: every free voxel inside the ellipsoid, connectivity ignored (leaks through thin walls)
	Geodesic,	// B: breadth-first fill through free voxels, clipped to the ellipsoid
	Volume,		// C: fill in distance order until the open-space volume is used up (spreads down corridors)
};

/** How clients get the voxel result (report chapter 07). */
UENUM()
enum class ESLSyncMode : uint8
{
	ServerResult,	// server fills, sends the compressed result
	LocalFromInputs,	// server sends origin + rule only, each client fills with its own copy of the level
};

namespace SL
{
	static constexpr float VoxelCm = 50.f;			// ~20 Hammer units
	static constexpr int32 N = 32;
	static constexpr int32 N3 = N * N * N;
	static constexpr float RadiusXY = 7.5f;			// voxels (3.75 m)
	static constexpr float RadiusZ = 4.5f;			// voxels (2.25 m)
	static constexpr float GrowSeconds = 1.6f;
	static constexpr float LifeSeconds = 18.f;
	static constexpr float FadeSeconds = 2.5f;
	static constexpr float BulletLifeSeconds = 1.6f;	// tunnel age 0 -> 1
	static constexpr float BlastLifeSeconds = 5.5f;
	static constexpr float Hammer = 2.54f;
	static constexpr uint8 Empty = 255;
	static constexpr float ArrivalScale = 8.f;		// stored byte = distance (voxels) * 8
	static constexpr float BotSmokeBlockCm = 5.f * 100.f;	// CS2 bots: 200 units of smoke blocks sight; we use 5 m of full density
}

/** Settings everyone shares (server-owned, replicated). */
USTRUCT()
struct FSLSharedSettings
{
	GENERATED_BODY()
	UPROPERTY() ESLSpreadRule Rule = ESLSpreadRule::Volume;
	UPROPERTY() ESLSyncMode Sync = ESLSyncMode::ServerResult;
	UPROPERTY() bool bBullets = true;
	UPROPERTY() bool bBlasts = true;
	UPROPERTY() bool bRefill = true;
	UPROPERTY() bool bRelevancyCull = false;
	UPROPERTY() bool bBotsFire = true;
	UPROPERTY() bool bSlowGrowth = false;
};

USTRUCT()
struct FSLBulletEvent
{
	GENERATED_BODY()
	UPROPERTY() FVector_NetQuantize Start;
	UPROPERTY() FVector_NetQuantize End;
	UPROPERTY() float Time = 0.f;		// server world time
	UPROPERTY() uint8 K10 = 10;			// radius scale * 10 (shotgun-like spray uses 5)
	UPROPERTY() int32 Shooter = -1;		// player id, used to skip the local copy
};

USTRUCT()
struct FSLBlastEvent
{
	GENERATED_BODY()
	UPROPERTY() FVector_NetQuantize Pos;
	UPROPERTY() float Time = 0.f;
	UPROPERTY() uint16 Mask = 0;		// smoke bits the blast reaches (line of sight, not through walls)
};
