#include "SLVoxel.h"

#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/Compression.h"

namespace
{
	const ECollisionChannel SmokeChannel = ECC_GameTraceChannel1;
	const float TestHalf = 21.f;	// a voxel is solid when geometry reaches into its central 42 cm
}

uint8 FSLVoxelWorld::Test(UWorld* World, const FIntVector& V) const
{
	TArray<FOverlapResult> Hits;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(SLVoxel), false);
	World->OverlapMultiByChannel(Hits, Center(V), FQuat::Identity, SmokeChannel, FCollisionShape::MakeBox(FVector(TestHalf)), Q);
	uint8 R = 0;
	for (const FOverlapResult& H : Hits)
	{
		if (!H.bBlockingHit) continue;
		const AActor* A = H.GetActor();
		if (A && A->ActorHasTag(TEXT("SmokePass"))) { R = FMath::Max<uint8>(R, 2); continue; }
		return 1;
	}
	return R;
}

void FSLVoxelWorld::Build(UWorld* World, const FBox& Bounds)
{
	Origin = Bounds.Min;
	const FVector Size = Bounds.GetSize() / SL::VoxelCm;
	Dim = FIntVector(FMath::CeilToInt(Size.X), FMath::CeilToInt(Size.Y), FMath::CeilToInt(Size.Z));
	Cells.SetNumZeroed(Dim.X * Dim.Y * Dim.Z);
	for (int32 Z = 0; Z < Dim.Z; ++Z) for (int32 Y = 0; Y < Dim.Y; ++Y) for (int32 X = 0; X < Dim.X; ++X)
		Cells[Index(X, Y, Z)] = Test(World, FIntVector(X, Y, Z));
}

int32 FSLVoxelWorld::Rebuild(UWorld* World, const FBox& Region)
{
	if (!IsValid()) return 0;
	const FIntVector A = ToVoxel(Region.Min), B = ToVoxel(Region.Max);
	int32 Changed = 0;
	for (int32 Z = FMath::Max(A.Z, 0); Z <= FMath::Min(B.Z, Dim.Z - 1); ++Z)
	for (int32 Y = FMath::Max(A.Y, 0); Y <= FMath::Min(B.Y, Dim.Y - 1); ++Y)
	for (int32 X = FMath::Max(A.X, 0); X <= FMath::Min(B.X, Dim.X - 1); ++X)
	{
		const uint8 C = Test(World, FIntVector(X, Y, Z));
		uint8& Cell = Cells[Index(X, Y, Z)];
		if (Cell != C) { Cell = C; ++Changed; }
	}
	return Changed;
}

uint32 FSLVoxelWorld::Hash() const
{
	return FCrc::MemCrc32(Cells.GetData(), Cells.Num());
}

TArray<uint8> FSLVoxelWorld::SolidMask() const
{
	TArray<uint8> M;
	M.SetNumUninitialized(Cells.Num());
	for (int32 I = 0; I < Cells.Num(); ++I) M[I] = Cells[I] == 1 ? 1 : 0;
	return M;
}

// ---------------------------------------------------------------- fill

namespace
{
	constexpr float VScale = SL::RadiusXY / SL::RadiusZ;	// vertical steps cost more: the cloud is wider than tall
	inline int32 L(int32 X, int32 Y, int32 Z) { return (Z * SL::N + Y) * SL::N + X; }
	inline bool InGrid(int32 X, int32 Y, int32 Z) { return X >= 0 && Y >= 0 && Z >= 0 && X < SL::N && Y < SL::N && Z < SL::N; }
	inline bool InEllipsoid(const FIntVector& D)	// D relative to the seed; the ellipsoid centre sits 2 voxels above it
	{
		const float X = D.X / SL::RadiusXY, Y = D.Y / SL::RadiusXY, Z = (D.Z - 2) / SL::RadiusZ;
		return X * X + Y * Y + Z * Z <= 1.f;
	}
	inline uint8 Encode(float Dist) { return (uint8)FMath::Clamp(FMath::RoundToInt(Dist * SL::ArrivalScale), 0, 254); }
}

int32 SLFill::OpenVolume()
{
	static int32 Cached = 0;
	if (Cached) return Cached;
	int32 C = 0;
	for (int32 Z = 0; Z < SL::N; ++Z) for (int32 Y = -16; Y < 16; ++Y) for (int32 X = -16; X < 16; ++X)
		if (InEllipsoid(FIntVector(X, Y, Z))) ++C;
	Cached = C;
	return C;
}

FSLFillResult SLFill::Run(const FSLVoxelWorld& W, const FVector& Detonation, ESLSpreadRule Rule)
{
	const double T0 = FPlatformTime::Seconds();
	FSLFillResult R;
	R.Arrival.Init(SL::Empty, SL::N3);
	if (!W.IsValid()) return R;

	// seed: the voxel holding the grenade, or the nearest free one within two voxels (a grenade wedged in a crevice)
	FIntVector Seed = W.ToVoxel(Detonation + FVector(0, 0, 10));
	if (W.Blocks(Seed.X, Seed.Y, Seed.Z))
	{
		int32 Best = MAX_int32;
		FIntVector Found = Seed;
		for (int32 Z = -2; Z <= 2; ++Z) for (int32 Y = -2; Y <= 2; ++Y) for (int32 X = -2; X <= 2; ++X)
		{
			const int32 D2 = X * X + Y * Y + Z * Z;
			if (D2 < Best && !W.Blocks(Seed.X + X, Seed.Y + Y, Seed.Z + Z)) { Best = D2; Found = Seed + FIntVector(X, Y, Z); }
		}
		if (Best == MAX_int32) return R;	// "failed to find detonation"
		Seed = Found;
	}
	R.Seed = Seed;
	R.GridOrigin = Seed - FIntVector(16, 16, 4);
	const FIntVector GS(16, 16, 4);	// seed in grid coordinates
	auto WorldBlocks = [&](int32 X, int32 Y, int32 Z) { const FIntVector V = R.GridOrigin + FIntVector(X, Y, Z); return W.Blocks(V.X, V.Y, V.Z); };

	// reachability through free voxels inside the ellipsoid (used by A for the leak count, and it *is* rule B)
	TArray<int16> Steps;
	Steps.Init(-1, SL::N3);
	{
		TArray<int32> Q;
		Q.Reserve(4096);
		Steps[L(GS.X, GS.Y, GS.Z)] = 0;
		Q.Add(L(GS.X, GS.Y, GS.Z));
		static const int32 D6[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
		for (int32 H = 0; H < Q.Num(); ++H)
		{
			const int32 I = Q[H], X = I % SL::N, Y = (I / SL::N) % SL::N, Z = I / (SL::N * SL::N);
			for (const auto& O : D6)
			{
				const int32 NX = X + O[0], NY = Y + O[1], NZ = Z + O[2];
				if (!InGrid(NX, NY, NZ)) continue;
				const int32 J = L(NX, NY, NZ);
				if (Steps[J] >= 0 || WorldBlocks(NX, NY, NZ) || !InEllipsoid(FIntVector(NX, NY, NZ) - GS)) continue;
				Steps[J] = Steps[I] + 1;
				Q.Add(J);
			}
		}
	}

	if (Rule == ESLSpreadRule::Ellipsoid)
	{
		for (int32 Z = 0; Z < SL::N; ++Z) for (int32 Y = 0; Y < SL::N; ++Y) for (int32 X = 0; X < SL::N; ++X)
		{
			const FIntVector D = FIntVector(X, Y, Z) - GS;
			if (WorldBlocks(X, Y, Z) || !InEllipsoid(D)) continue;
			const float Dist = FMath::Sqrt(float(D.X * D.X + D.Y * D.Y) + FMath::Square(D.Z * VScale));
			R.Arrival[L(X, Y, Z)] = Encode(Dist);
			R.MaxDist = FMath::Max(R.MaxDist, Dist);
			++R.Filled;
			if (Steps[L(X, Y, Z)] < 0) ++R.Leaked;
		}
	}
	else if (Rule == ESLSpreadRule::Geodesic)
	{
		for (int32 I = 0; I < SL::N3; ++I)
			if (Steps[I] >= 0)
			{
				R.Arrival[I] = Encode(Steps[I]);
				R.MaxDist = FMath::Max(R.MaxDist, float(Steps[I]));
				++R.Filled;
			}
	}
	else
	{
		// integer chamfer Dijkstra (costs x10), 26 neighbours, no cutting through solid edges, bucket queue
		const int32 Budget = OpenVolume();
		TArray<int32> Dist;
		Dist.Init(MAX_int32, SL::N3);
		TArray<uint8> Done;
		Done.Init(0, SL::N3);
		TArray<TArray<int32>> Buckets;
		Buckets.SetNum(2048);
		const int32 S0 = L(GS.X, GS.Y, GS.Z);
		Dist[S0] = 0;
		Buckets[0].Add(S0);
		int32 Cost[3][3][3];
		for (int32 Z = -1; Z <= 1; ++Z) for (int32 Y = -1; Y <= 1; ++Y) for (int32 X = -1; X <= 1; ++X)
			Cost[Z + 1][Y + 1][X + 1] = FMath::RoundToInt(10.f * FMath::Sqrt(float(X * X + Y * Y) + FMath::Square(Z * VScale)));
		for (int32 B = 0; B < Buckets.Num() && R.Filled < Budget; ++B)
		{
			for (int32 K = 0; K < Buckets[B].Num() && R.Filled < Budget; ++K)
			{
				const int32 I = Buckets[B][K];
				if (Done[I] || Dist[I] != B) continue;
				Done[I] = 1;
				R.Arrival[I] = Encode(B / 10.f);
				R.MaxDist = FMath::Max(R.MaxDist, B / 10.f);
				++R.Filled;
				const int32 X = I % SL::N, Y = (I / SL::N) % SL::N, Z = I / (SL::N * SL::N);
				for (int32 DZ = -1; DZ <= 1; ++DZ) for (int32 DY = -1; DY <= 1; ++DY) for (int32 DX = -1; DX <= 1; ++DX)
				{
					if (!DX && !DY && !DZ) continue;
					const int32 NX = X + DX, NY = Y + DY, NZ = Z + DZ;
					if (!InGrid(NX, NY, NZ) || WorldBlocks(NX, NY, NZ)) continue;
					// a diagonal step needs every axis-aligned step it is made of to be free
					if ((DX && WorldBlocks(X + DX, Y, Z)) || (DY && WorldBlocks(X, Y + DY, Z)) || (DZ && WorldBlocks(X, Y, Z + DZ))) continue;
					const int32 J = L(NX, NY, NZ);
					if (Done[J]) continue;
					const int32 ND = B + Cost[DZ + 1][DY + 1][DX + 1];
					if (ND < Dist[J] && ND < Buckets.Num()) { Dist[J] = ND; Buckets[ND].Add(J); }
				}
			}
		}
	}
	R.bOk = R.Filled > 0;
	R.Micros = (FPlatformTime::Seconds() - T0) * 1e6;
	return R;
}

TArray<uint8> SLFill::Pack(const TArray<uint8>& Arrival)
{
	TArray<uint8> Out;
	int32 Size = FCompression::CompressMemoryBound(NAME_Zlib, Arrival.Num());
	Out.SetNumUninitialized(Size + 4);
	const uint32 Raw = Arrival.Num();
	FMemory::Memcpy(Out.GetData(), &Raw, 4);
	if (!FCompression::CompressMemory(NAME_Zlib, Out.GetData() + 4, Size, Arrival.GetData(), Arrival.Num(), COMPRESS_BiasSize)) return TArray<uint8>();
	Out.SetNum(Size + 4);
	return Out;
}

bool SLFill::Unpack(const TArray<uint8>& Packed, TArray<uint8>& OutArrival)
{
	if (Packed.Num() < 5) return false;
	uint32 Raw = 0;
	FMemory::Memcpy(&Raw, Packed.GetData(), 4);
	if (Raw != SL::N3) return false;
	OutArrival.SetNumUninitialized(Raw);
	return FCompression::UncompressMemory(NAME_Zlib, OutArrival.GetData(), Raw, Packed.GetData() + 4, Packed.Num() - 4);
}
