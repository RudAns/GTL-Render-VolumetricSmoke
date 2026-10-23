#pragma once

#include "CoreMinimal.h"
#include "SLTypes.h"

/** The level as seen by the smoke: one byte per 50 cm voxel (0 air, 1 solid, 2 smoke-permeable such as a grate). */
struct SMOKELAB_API FSLVoxelWorld
{
	FVector Origin = FVector::ZeroVector;
	FIntVector Dim = FIntVector::ZeroValue;
	TArray<uint8> Cells;
	bool bGrateIsSolid = false;		// "different level data" injected on a client to show desync

	bool IsValid() const { return Cells.Num() > 0; }
	int32 Index(int32 X, int32 Y, int32 Z) const { return (Z * Dim.Y + Y) * Dim.X + X; }
	bool Inside(int32 X, int32 Y, int32 Z) const { return X >= 0 && Y >= 0 && Z >= 0 && X < Dim.X && Y < Dim.Y && Z < Dim.Z; }
	bool Blocks(int32 X, int32 Y, int32 Z) const
	{
		if (!Inside(X, Y, Z)) return Z >= 0 ? false : true;	// open sky above and around, solid below
		const uint8 C = Cells[Index(X, Y, Z)];
		return C == 1 || (C == 2 && bGrateIsSolid);
	}
	FIntVector ToVoxel(const FVector& P) const
	{
		const FVector L = (P - Origin) / SL::VoxelCm;
		return FIntVector(FMath::FloorToInt(L.X), FMath::FloorToInt(L.Y), FMath::FloorToInt(L.Z));
	}
	FVector Corner(const FIntVector& V) const { return Origin + FVector(V) * SL::VoxelCm; }
	FVector Center(const FIntVector& V) const { return Corner(V) + FVector(SL::VoxelCm * 0.5f); }

	/** Overlap-test every voxel against the "Smoke" collision channel; actors tagged SmokePass become permeable. */
	void Build(UWorld* World, const FBox& Bounds);
	/** Re-test only the voxels inside Region (doors). Returns the number of changed voxels. */
	int32 Rebuild(UWorld* World, const FBox& Region);
	uint32 Hash() const;
	/** Solid mask for the renderer (sun occlusion). */
	TArray<uint8> SolidMask() const;
private:
	uint8 Test(UWorld* World, const FIntVector& V) const;
};

/** Result of one fill: arrival distance per voxel of the smoke's 32^3 grid. */
struct SMOKELAB_API FSLFillResult
{
	FIntVector GridOrigin = FIntVector::ZeroValue;	// world voxel of grid cell (0,0,0)
	FIntVector Seed = FIntVector::ZeroValue;			// world voxel the fill started from
	TArray<uint8> Arrival;							// N^3 bytes, SL::Empty = no smoke
	int32 Filled = 0;
	int32 Leaked = 0;		// filled voxels not reachable from the seed without crossing solid (rule A only)
	float MaxDist = 0.f;	// voxels
	double Micros = 0.0;
	bool bOk = false;
};

namespace SLFill
{
	/** Fill from a world position. Deterministic: integer costs, fixed iteration order. */
	SMOKELAB_API FSLFillResult Run(const FSLVoxelWorld& W, const FVector& Detonation, ESLSpreadRule Rule);
	/** Number of voxels the ellipsoid holds above an open floor: the volume budget of rule C. */
	SMOKELAB_API int32 OpenVolume();
	SMOKELAB_API TArray<uint8> Pack(const TArray<uint8>& Arrival);
	SMOKELAB_API bool Unpack(const TArray<uint8>& Packed, TArray<uint8>& OutArrival);
}
