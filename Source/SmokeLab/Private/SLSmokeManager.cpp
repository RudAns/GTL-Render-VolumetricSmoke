#include "SLSmokeManager.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "SceneViewExtension.h"
#include "SLLevel.h"
#include "SLSmoke.h"
#include "UObject/ConstructorHelpers.h"

ASLSmokeManager::ASLSmokeManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(30.f);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	VoxelCubes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("VoxelCubes"));
	VoxelCubes->SetupAttachment(RootComponent);
	LevelCubes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("LevelCubes"));
	LevelCubes->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (UInstancedStaticMeshComponent* C : { VoxelCubes.Get(), LevelCubes.Get() })
	{
		if (Cube.Succeeded()) C->SetStaticMesh(Cube.Object);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->NumCustomDataFloats = 3;
		C->SetMobility(EComponentMobility::Movable);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VoxMat(TEXT("/Game/SmokeLab/Materials/M_Voxel.M_Voxel"));
	if (VoxMat.Succeeded()) { VoxelCubes->SetMaterial(0, VoxMat.Object); LevelCubes->SetMaterial(0, VoxMat.Object); }
}

ASLSmokeManager* ASLSmokeManager::Get(const UObject* Ctx)
{
	UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<ASLSmokeManager> It(W); It; ++It) return *It;
	return nullptr;
}

void ASLSmokeManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASLSmokeManager, Settings);
	DOREPLIFETIME(ASLSmokeManager, Bullets);
	DOREPLIFETIME(ASLSmokeManager, Blasts);
}

void ASLSmokeManager::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
		Renderer = FSceneViewExtensions::NewExtension<FSLSmokeRenderer>(GetWorld());
}

void ASLSmokeManager::EndPlay(const EEndPlayReason::Type Reason)
{
	Renderer.Reset();
	Super::EndPlay(Reason);
}

float ASLSmokeManager::Now() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? (float)GS->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
}

bool ASLSmokeManager::EnsureWorld()
{
	if (World.IsValid()) return true;
	ASLLevel* Lvl = ASLLevel::Get(this);
	if (!Lvl || !Lvl->IsBuilt()) return false;
	const double T0 = FPlatformTime::Seconds();
	World.Build(GetWorld(), ASLLevel::VoxelBounds());
	WorldBuildMs = int32((FPlatformTime::Seconds() - T0) * 1000.0);
	World.bGrateIsSolid = View.bInjectDiff && !HasAuthority();
	if (Renderer) Renderer->SetWorldOccupancy(World.Origin, World.Dim, SL::VoxelCm, World.SolidMask());
	UE_LOG(LogTemp, Log, TEXT("SLTEST voxel world %dx%dx%d built in %d ms, crc %08x"), World.Dim.X, World.Dim.Y, World.Dim.Z, WorldBuildMs, World.Hash());
	return true;
}

void ASLSmokeManager::RebuildRegion(const FBox& Region)
{
	if (!World.IsValid()) return;
	World.Rebuild(GetWorld(), Region);
	if (Renderer) Renderer->SetWorldOccupancy(World.Origin, World.Dim, SL::VoxelCm, World.SolidMask());
}

void ASLSmokeManager::SetInjectDiff(bool b)
{
	View.bInjectDiff = b;
	World.bGrateIsSolid = b && !HasAuthority();
	// clients that fill locally redo it with the altered level copy
	for (const TWeakObjectPtr<ASLSmoke>& S : Smokes)
		if (S.IsValid() && !HasAuthority() && S->Sync == ESLSyncMode::LocalFromInputs) S->FillLocally();
}

void ASLSmokeManager::RegisterSmoke(ASLSmoke* S) { Smokes.AddUnique(S); }
void ASLSmokeManager::UnregisterSmoke(ASLSmoke* S) { Smokes.Remove(S); }

ASLSmoke* ASLSmokeManager::SpawnSmoke(const FVector& Where)
{
	if (!HasAuthority() || !EnsureWorld()) return nullptr;
	// pick a free bit (= atlas slot); the oldest smoke gives its slot up when all 16 are in use
	uint32 Used = 0;
	ASLSmoke* Oldest = nullptr;
	for (const TWeakObjectPtr<ASLSmoke>& S : Smokes)
		if (S.IsValid())
		{
			Used |= 1u << S->Bit;
			if (!Oldest || S->StartTime < Oldest->StartTime) Oldest = S.Get();
		}
	int32 Bit = 0;
	while (Bit < SLRender::MaxSlots && (Used & (1u << Bit))) ++Bit;
	if (Bit >= SLRender::MaxSlots) { Bit = Oldest->Bit; Oldest->Destroy(); }
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASLSmoke* S = GetWorld()->SpawnActor<ASLSmoke>(Where, FRotator::ZeroRotator, P);
	if (S) S->ServerInit(Where, Settings.Rule, Settings.Sync, Now(), (uint8)Bit, Settings.bSlowGrowth);
	return S;
}

void ASLSmokeManager::AddBullet(const FVector& Start, const FVector& End, float RadiusScale, int32 Shooter)
{
	if (!HasAuthority() || !Settings.bBullets) return;
	// only shots that actually pass through a smoke matter to anyone
	bool bTouches = false;
	for (const TWeakObjectPtr<ASLSmoke>& S : Smokes)
		if (S.IsValid() && FMath::LineBoxIntersection(S->Bounds(), Start, End, End - Start)) { bTouches = true; break; }
	if (!bTouches) return;
	FSLBulletEvent E;
	E.Start = Start; E.End = End; E.Time = Now(); E.K10 = (uint8)FMath::Clamp(FMath::RoundToInt(RadiusScale * 10.f), 1, 255); E.Shooter = Shooter;
	Bullets.Add(E);
	while (Bullets.Num() > SLRender::MaxBullets) Bullets.RemoveAt(0);
}

void ASLSmokeManager::AddLocalBullet(const FVector& Start, const FVector& End, float RadiusScale, int32 Shooter)
{
	FSLBulletEvent E;
	E.Start = Start; E.End = End; E.Time = Now(); E.K10 = (uint8)FMath::Clamp(FMath::RoundToInt(RadiusScale * 10.f), 1, 255); E.Shooter = Shooter;
	LocalBullets.Add(E);
	while (LocalBullets.Num() > 8) LocalBullets.RemoveAt(0);
}

void ASLSmokeManager::AddBlast(const FVector& Where)
{
	if (!HasAuthority()) return;
	FSLBlastEvent E;
	E.Pos = Where; E.Time = Now();
	// a blast only pushes smokes it can see (CS2 fixed "HE grenades affect smokes through walls")
	for (const TWeakObjectPtr<ASLSmoke>& S : Smokes)
	{
		if (!S.IsValid() || !S->HasField()) continue;
		const FBox B = S->Bounds();
		if (!B.ExpandBy(250.f * SL::Hammer).IsInside(Where)) continue;
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(SLBlast), false);
		const FVector Target = S->Detonation + FVector(0, 0, 80);
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Where + FVector(0, 0, 30), Target, ECC_GameTraceChannel1, Q);
		if (!bBlocked || S->Density(Where, Now()) > 0.05f) E.Mask |= 1u << S->Bit;
	}
	if (!Settings.bBlasts) E.Mask = 0;
	Blasts.Add(E);
	while (Blasts.Num() > SLRender::MaxBlasts) Blasts.RemoveAt(0);
}

void ASLSmokeManager::OnDoorChanged(ASLDoor* Door)
{
	if (!Door) return;
	RebuildRegion(Door->Region());
	const FBox R = Door->Region();
	for (const TWeakObjectPtr<ASLSmoke>& S : Smokes)
	{
		if (!S.IsValid() || !S->Bounds().Intersect(R)) continue;
		if (HasAuthority()) S->ServerRefill();
		else if (S->Sync == ESLSyncMode::LocalFromInputs) S->FillLocally();
	}
}

void ASLSmokeManager::ResetRound()
{
	if (!HasAuthority()) return;
	TArray<TWeakObjectPtr<ASLSmoke>> Copy = Smokes;
	for (const TWeakObjectPtr<ASLSmoke>& S : Copy) if (S.IsValid()) S->Destroy();
	Bullets.Reset();
	Blasts.Reset();
}

// ---------------------------------------------------------------- gameplay density field (CPU mirror of the shader, no noise)

float ASLSmokeManager::BulletCarve(const FVector& P, const TArray<FSLBulletEvent>& List) const
{
	float C = 0.f;
	const float T = Now();
	for (const FSLBulletEvent& B : List)
	{
		const float Age = (T - B.Time) / SL::BulletLifeSeconds;
		if (Age < 0.f || Age >= 1.f) continue;
		const FVector SE = B.End - B.Start;
		const float H = FMath::Clamp(FVector::DotProduct(P - B.Start, SE) / FMath::Max(SE.SizeSquared(), 1.0), 0.0, 1.0);
		const float R = FMath::Clamp(float(FVector::Dist(P, B.Start + SE * H)) * (0.05f / SL::Hammer) * (B.K10 / 10.f), 0.f, 1.f);
		if (R >= 1.f) continue;
		const float NearEnd = 1.f - FMath::Clamp(float(FVector::Dist(P, (FVector)B.End)) * (0.01f / SL::Hammer), 0.f, 1.f);
		const float X = 1.f - FMath::Clamp(Age + FMath::Clamp(R + NearEnd, 0.f, 1.f), 0.f, 1.f);
		C = FMath::Max(C, FMath::SmoothStep(0.f, 0.5f, X));	// same curve as the shader's BulletCarve
	}
	return C;
}

float ASLSmokeManager::BlastFactor(const FVector& P, int32 Bit) const
{
	float M = 1.f;
	const float T = Now();
	for (const FSLBlastEvent& B : Blasts)
	{
		if (!(B.Mask & (1u << Bit))) continue;
		const float Age = T - B.Time;
		if (Age < 0.f || Age > SL::BlastLifeSeconds) continue;
		const float D = FVector::Dist(P, (FVector)B.Pos);
		if (D > 250.f * SL::Hammer) continue;
		const float Pulse = FMath::Pow(1.f - FMath::SmoothStep(0.f, 2.f, Age), 128.f);
		float K = FMath::SmoothStep(200.f * SL::Hammer, 240.f * SL::Hammer, D + Pulse * 250.f * SL::Hammer);
		K += Settings.bRefill ? FMath::Pow(FMath::SmoothStep(0.5f, 5.f, Age), 1.8f) : 0.f;
		M = FMath::Min(M, FMath::Lerp(0.02f, 1.f, FMath::Clamp(K, 0.f, 1.f)));
	}
	return M;
}

float ASLSmokeManager::DensityAt(const FVector& P, bool bCarve) const
{
	const float T = Now();
	float D = 0.f;
	for (const TWeakObjectPtr<ASLSmoke>& S : Smokes)
	{
		if (!S.IsValid()) continue;
		float Di = S->Density(P, T);
		if (Di <= 0.f) continue;
		if (bCarve) Di *= BlastFactor(P, S->Bit);
		D += Di;
	}
	if (bCarve && D > 0.f)
	{
		const float C = FMath::Max(BulletCarve(P, Bullets), BulletCarve(P, LocalBullets));
		D = D + (-0.05f - D) * C;
	}
	return FMath::Clamp(D, 0.f, 1.f);
}

float ASLSmokeManager::SmokeLength(const FVector& A, const FVector& B, bool bCarve) const
{
	// only march where the segment crosses a smoke grid
	float Len = 0.f;
	const FVector Dir = B - A;
	const float Total = Dir.Size();
	if (Total < 1.f) return 0.f;
	float T0 = 1.f, T1 = 0.f;
	for (const TWeakObjectPtr<ASLSmoke>& S : Smokes)
	{
		if (!S.IsValid() || !S->HasField()) continue;
		FVector HitLoc, HitNorm; float HitT;
		const FBox Box = S->Bounds();
		if (Box.IsInside(A)) { T0 = 0.f; }
		else if (FMath::LineExtentBoxIntersection(Box, A, B, FVector::ZeroVector, HitLoc, HitNorm, HitT)) T0 = FMath::Min(T0, HitT);
		else continue;
		T1 = 1.f;	// conservative: march to the end once inside any box
	}
	if (T1 <= T0) return 0.f;
	const float Step = 20.f;
	for (float S = T0 * Total; S < Total; S += Step)
		Len += DensityAt(A + Dir * (S / Total), bCarve) * Step;
	return Len;
}

// ---------------------------------------------------------------- per frame

void ASLSmokeManager::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bWorldTried || !World.IsValid()) { bWorldTried = EnsureWorld(); }
	Smokes.RemoveAll([](const TWeakObjectPtr<ASLSmoke>& S) { return !S.IsValid(); });
	// local predicted shots: drop the copy once the server's version arrived (or the tunnel closed)
	const float T = Now();
	LocalBullets.RemoveAll([&](const FSLBulletEvent& L)
	{
		if (T - L.Time > SL::BulletLifeSeconds) return true;
		for (const FSLBulletEvent& B : Bullets)
			if (B.Shooter == L.Shooter && FVector::DistSquared(B.End, L.End) < 2500.f) return true;
		return false;
	});
	if (Renderer) FeedRenderer();
	UpdateVoxelViz(Dt);
}

void ASLSmokeManager::FeedRenderer()
{
	const float T = Now();
	FSLRenderFrame F;
	F.Settings = View.Render;
	F.Settings.bBullets = Settings.bBullets;
	F.Settings.bBlasts = Settings.bBlasts;
	F.Time = T;
	F.VoxelCm = SL::VoxelCm;
	// sun from the level's directional light
	if (const ASLLevel* Lvl = ASLLevel::Get(this))
		if (const UDirectionalLightComponent* L = Lvl->SunLight)
		{
			F.SunDir = -L->GetComponentRotation().Vector();
			F.SunIlluminance = L->GetLightColor() * L->Intensity;
		}
	if (const ASLLevel* Lvl = ASLLevel::Get(this))
		for (const UPointLightComponent* PL : Lvl->Lamps)
			if (PL && F.Lights.Num() < SLRender::MaxLights)
			{
				FSLRenderLight RL; RL.Pos = PL->GetComponentLocation(); RL.Candela = PL->GetLightColor() * PL->Intensity; RL.RadiusCm = PL->AttenuationRadius;
				F.Lights.Add(RL);
			}
	F.SkyIlluminance = FLinearColor(0.20f, 0.24f, 0.30f) * (F.SunIlluminance.GetLuminance() * 0.35f);

	for (const TWeakObjectPtr<ASLSmoke>& SP : Smokes)
	{
		ASLSmoke* S = SP.Get();
		if (!S || !S->HasField()) continue;
		const int32 Slot = S->Bit;
		int32& Seen = UploadedDirty.FindOrAdd(Slot, -1);
		TWeakObjectPtr<ASLSmoke>& SlotRef = SlotOwner.FindOrAdd(Slot);
		if (Seen != S->DirtyCount || SlotRef.Get() != S)
		{
			// two rings of soft falloff around the cloud so trilinear filtering gives a gradient, not a voxel step
			TArray<uint8> Up = S->Field.Arrival;
			for (int32 Ring = 1; Ring <= 2; ++Ring)
			{
				TArray<uint8> Src = Up;
				for (int32 I = 0; I < SL::N3; ++I)
				{
					if (Src[I] != SL::Empty) continue;
					const int32 X = I % SL::N, Y = (I / SL::N) % SL::N, Z = I / (SL::N * SL::N);
					const FIntVector V = S->Field.GridOrigin + FIntVector(X, Y, Z);
					if (World.Blocks(V.X, V.Y, V.Z)) continue;	// never soften into (or through) a wall
					int32 Best = 999;
					static const int32 D6[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
					for (const auto& D : D6)
					{
						const int32 NX = X + D[0], NY = Y + D[1], NZ = Z + D[2];
						if (NX < 0 || NY < 0 || NZ < 0 || NX >= SL::N || NY >= SL::N || NZ >= SL::N) continue;
						const uint8 Nv = Src[(NZ * SL::N + NY) * SL::N + NX];
						if (Nv != SL::Empty) Best = FMath::Min<int32>(Best, Nv);
					}
					if (Best < 999) Up[I] = (uint8)FMath::Min(Best + 12, 254);
				}
			}
			Renderer->UploadSlot(Slot, Up);
			Seen = S->DirtyCount;
			SlotRef = S;
		}
		FSLRenderSmoke R;
		R.GridOrigin = S->GridOriginWorld();
		R.Slot = Slot;
		R.Bit = S->Bit;
		R.Radius = S->Radius(T);
		R.Feather = 2.0f;
		R.Density = S->DensityScale(T);
		F.Smokes.Add(R);
	}
	auto AddBullets = [&](const TArray<FSLBulletEvent>& List)
	{
		for (const FSLBulletEvent& B : List)
		{
			const float Age = (T - B.Time) / SL::BulletLifeSeconds;
			if (Age < 0.f || Age >= 1.f || F.Bullets.Num() >= SLRender::MaxBullets) continue;
			FSLRenderBullet R; R.Start = B.Start; R.End = B.End; R.Age = Age; R.RadiusScale = B.K10 / 10.f;
			F.Bullets.Add(R);
		}
	};
	AddBullets(LocalBullets);
	AddBullets(Bullets);
	for (const FSLBlastEvent& B : Blasts)
	{
		const float Age = T - B.Time;
		if (Age < 0.f || Age > SL::BlastLifeSeconds) continue;
		FSLRenderBlast R; R.Pos = B.Pos; R.Age = Settings.bRefill ? Age : FMath::Min(Age, 0.4f); R.Mask = B.Mask;
		F.Blasts.Add(R);
	}
	Renderer->SetFrame(F);
	GpuTimes = Renderer->GetGpuTimes();
}

void ASLSmokeManager::UpdateVoxelViz(float Dt)
{
	VoxelVizTimer -= Dt;
	if (VoxelVizTimer > 0.f) return;
	VoxelVizTimer = 0.25f;
	VoxelCubes->ClearInstances();
	LevelCubes->ClearInstances();
	if (!View.bShowVoxels && !View.bShowLevelVoxels) return;
	const float T = Now();
	const float S = SL::VoxelCm / 100.f;
	TArray<FTransform> Xf;
	TArray<float> Custom;
	for (const TWeakObjectPtr<ASLSmoke>& SP : Smokes)
	{
		const ASLSmoke* Sm = SP.Get();
		if (!Sm || !Sm->HasField()) continue;
		const FVector O = Sm->GridOriginWorld();
		const float R = Sm->Radius(T);
		for (int32 I = 0; I < SL::N3; ++I)
		{
			const uint8 A = Sm->Field.Arrival[I];
			const int32 X = I % SL::N, Y = (I / SL::N) % SL::N, Z = I / (SL::N * SL::N);
			if (View.bShowVoxels && A != SL::Empty && A / SL::ArrivalScale <= R)
			{
				const float H = FMath::Clamp((A / SL::ArrivalScale) / FMath::Max(Sm->Field.MaxDist, 1.f), 0.f, 1.f);
				const FLinearColor C = FLinearColor::LerpUsingHSV(FLinearColor(0.1f, 0.85f, 1.f), FLinearColor(1.f, 0.35f, 0.1f), H);
				VoxelCubes->AddInstance(FTransform(FRotator::ZeroRotator, O + (FVector(X, Y, Z) + 0.5f) * SL::VoxelCm, FVector(S * 0.42f)), true);
				const int32 Last = VoxelCubes->GetInstanceCount() - 1;
				VoxelCubes->SetCustomDataValue(Last, 0, C.R); VoxelCubes->SetCustomDataValue(Last, 1, C.G); VoxelCubes->SetCustomDataValue(Last, 2, C.B);
			}
			if (View.bShowLevelVoxels)
			{
				const FIntVector V = Sm->Field.GridOrigin + FIntVector(X, Y, Z);
				if (World.Inside(V.X, V.Y, V.Z) && World.Cells[World.Index(V.X, V.Y, V.Z)] != 0)
				{
					// only the surface layer facing air, so the walls read as shells
					bool bSurface = false;
					static const int32 D6[6][3] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
					for (const auto& D : D6) bSurface |= !World.Blocks(V.X + D[0], V.Y + D[1], V.Z + D[2]);
					if (!bSurface) continue;
					const bool bGrate = World.Cells[World.Index(V.X, V.Y, V.Z)] == 2;
					const FLinearColor C = bGrate ? (World.bGrateIsSolid ? FLinearColor(1.f, 0.2f, 0.15f) : FLinearColor(0.3f, 1.f, 0.4f)) : FLinearColor(0.35f, 0.35f, 0.4f);
					LevelCubes->AddInstance(FTransform(FRotator::ZeroRotator, World.Center(V), FVector(S * 0.47f)), true);
					const int32 Last = LevelCubes->GetInstanceCount() - 1;
					LevelCubes->SetCustomDataValue(Last, 0, C.R); LevelCubes->SetCustomDataValue(Last, 1, C.G); LevelCubes->SetCustomDataValue(Last, 2, C.B);
				}
			}
		}
	}
}
