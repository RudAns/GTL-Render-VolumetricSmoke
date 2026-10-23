#include "SLSmoke.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "SLSmokeManager.h"
#include "UObject/ConstructorHelpers.h"

ASLSmoke::ASLSmoke()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASLSmoke::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASLSmoke, Detonation);
	DOREPLIFETIME(ASLSmoke, Rule);
	DOREPLIFETIME(ASLSmoke, Sync);
	DOREPLIFETIME(ASLSmoke, Version);
	DOREPLIFETIME(ASLSmoke, StartTime);
	DOREPLIFETIME(ASLSmoke, Bit);
	DOREPLIFETIME(ASLSmoke, bSlowGrowth);
	DOREPLIFETIME(ASLSmoke, GridOrigin);
	DOREPLIFETIME(ASLSmoke, Packed);
	DOREPLIFETIME(ASLSmoke, ServerFieldCrc);
}

void ASLSmoke::BeginPlay()
{
	Super::BeginPlay();
	if (ASLSmokeManager* M = ASLSmokeManager::Get(this)) M->RegisterSmoke(this);
}

void ASLSmoke::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ASLSmokeManager* M = ASLSmokeManager::Get(this)) M->UnregisterSmoke(this);
	Super::EndPlay(Reason);
}

void ASLSmoke::ServerInit(const FVector& Where, ESLSpreadRule InRule, ESLSyncMode InSync, float Now, uint8 InBit, bool bSlow)
{
	Detonation = Where;
	Rule = InRule;
	Sync = InSync;
	StartTime = Now;
	Bit = InBit;
	bSlowGrowth = bSlow;
	SetActorLocation(Where);
	ServerRefill();
}

void ASLSmoke::ServerRefill()
{
	ASLSmokeManager* M = ASLSmokeManager::Get(this);
	if (!M || !M->EnsureWorld()) return;
	FSLFillResult R = SLFill::Run(M->VoxelWorld(), Detonation, Rule);
	M->LastFillMicros = R.Micros;
	M->LastFillFilled = R.Filled;
	M->LastFillLeaked = R.Leaked;
	GridOrigin = R.GridOrigin;
	ServerFieldCrc = FCrc::MemCrc32(R.Arrival.GetData(), R.Arrival.Num());
	// the server always sends inputs; in the server-result mode it also sends the compressed field
	Packed = Sync == ESLSyncMode::ServerResult ? SLFill::Pack(R.Arrival) : TArray<uint8>();
	ReplicatedBytes = 24 + Packed.Num();
	++Version;
	SeenVersion = Version;
	SetField(MoveTemp(R), true);
}

void ASLSmoke::FillLocally()
{
	ASLSmokeManager* M = ASLSmokeManager::Get(this);
	if (!M || !M->EnsureWorld()) return;
	FSLFillResult R = SLFill::Run(M->VoxelWorld(), Detonation, Rule);
	SetField(MoveTemp(R), false);
}

void ASLSmoke::OnRep_Inputs()
{
	if (HasAuthority()) return;
	ReplicatedBytes = 24 + Packed.Num();
	if (Sync == ESLSyncMode::LocalFromInputs && SeenVersion != Version)
	{
		SeenVersion = Version;
		FillLocally();
	}
}

void ASLSmoke::OnRep_Packed()
{
	if (HasAuthority()) return;
	ReplicatedBytes = 24 + Packed.Num();
	if (Sync != ESLSyncMode::ServerResult || Packed.Num() == 0) return;
	FSLFillResult R;
	R.GridOrigin = GridOrigin;
	if (!SLFill::Unpack(Packed, R.Arrival)) return;
	R.bOk = true;
	for (uint8 B : R.Arrival) if (B != SL::Empty) { ++R.Filled; R.MaxDist = FMath::Max(R.MaxDist, B / SL::ArrivalScale); }
	SetField(MoveTemp(R), true);
}

void ASLSmoke::SetField(FSLFillResult&& R, bool bFromServer)
{
	Field = MoveTemp(R);
	bFieldFromServer = bFromServer;
	FieldCrc = FCrc::MemCrc32(Field.Arrival.GetData(), Field.Arrival.Num());
	++DirtyCount;
}

void ASLSmoke::Tick(float Dt)
{
	Super::Tick(Dt);
	ASLSmokeManager* M = ASLSmokeManager::Get(this);
	if (!M) return;
	// late joiners / ordering: make sure a field exists
	if (!HasAuthority() && !HasField())
	{
		if (Sync == ESLSyncMode::LocalFromInputs) FillLocally();
		else OnRep_Packed();
	}
	if (HasAuthority() && Age(M->Now()) > SL::LifeSeconds + 0.5f) Destroy();
}

float ASLSmoke::Radius(float Now) const
{
	const float T = Age(Now);
	if (T < 0.f || !HasField()) return 0.f;
	const float Grow = bSlowGrowth ? SL::GrowSeconds * 4.f : SL::GrowSeconds;
	const float X = FMath::Clamp(T / Grow, 0.f, 1.f);
	const float Ease = 1.f - FMath::Pow(1.f - X, 3.f);
	float R = (Field.MaxDist + 1.5f) * Ease;
	const float Tail = (SL::LifeSeconds - T) / SL::FadeSeconds;
	if (Tail < 1.f) R *= FMath::Clamp(0.35f + 0.65f * Tail, 0.f, 1.f);	// shrink from the outside, like CS2's fade
	return R;
}

float ASLSmoke::DensityScale(float Now) const
{
	const float Tail = (SL::LifeSeconds - Age(Now)) / SL::FadeSeconds;
	return FMath::Clamp(Tail, 0.f, 1.f);
}

FVector ASLSmoke::GridOriginWorld() const
{
	const ASLSmokeManager* M = ASLSmokeManager::Get(this);
	return M ? M->VoxelWorld().Corner(Field.GridOrigin) : FVector::ZeroVector;
}

FBox ASLSmoke::Bounds() const
{
	const FVector O = GridOriginWorld();
	return FBox(O, O + FVector(SL::N * SL::VoxelCm));
}

float ASLSmoke::Density(const FVector& P, float Now) const
{
	if (!HasField()) return 0.f;
	const FVector G = (P - GridOriginWorld()) / SL::VoxelCm;
	const int32 X = FMath::FloorToInt(G.X), Y = FMath::FloorToInt(G.Y), Z = FMath::FloorToInt(G.Z);
	if (X < 0 || Y < 0 || Z < 0 || X >= SL::N || Y >= SL::N || Z >= SL::N) return 0.f;
	const uint8 A = Field.Arrival[(Z * SL::N + Y) * SL::N + X];
	if (A == SL::Empty) return 0.f;
	const float R = Radius(Now);
	return FMath::Clamp((R - A / SL::ArrivalScale) / 1.5f, 0.f, 1.f) * DensityScale(Now);
}

// ---------------------------------------------------------------- door

ASLDoor::ASLDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(RootComponent);
	Leaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Leaf"));
	Leaf->SetupAttachment(Hinge);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded()) Leaf->SetStaticMesh(Cube.Object);
	Leaf->SetCollisionProfileName(TEXT("BlockAll"));
	Leaf->SetMobility(EComponentMobility::Movable);
}

void ASLDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASLDoor, bOpen);
}

void ASLDoor::Setup(const FVector& HingeLoc, float InWidth, float InHeight, float InClosedYaw, UMaterialInterface* Mat)
{
	Width = InWidth; Height = InHeight; ClosedYaw = InClosedYaw;
	SetActorLocation(HingeLoc);
	Hinge->SetRelativeRotation(FRotator(0, ClosedYaw, 0));
	// leaf extends along the hinge's +X
	Leaf->SetRelativeScale3D(FVector(Width / 100.f, 0.08f, Height / 100.f));
	Leaf->SetRelativeLocation(FVector(Width * 0.5f, 0, Height * 0.5f));
	if (Mat) Leaf->SetMaterial(0, Mat);
}

void ASLDoor::ServerToggle()
{
	bOpen = !bOpen;
	OnRep_Open();
}

void ASLDoor::OnRep_Open()
{
	bPendingRebuild = true;
	SettleTime = 0.f;
}

FBox ASLDoor::Region() const
{
	return FBox::BuildAABB(GetActorLocation() + FVector(0, 0, Height * 0.5f), FVector(Width + 60.f, Width + 60.f, Height * 0.5f + 30.f));
}

void ASLDoor::Tick(float Dt)
{
	Super::Tick(Dt);
	const float Target = bOpen ? 95.f : 0.f;
	Angle = FMath::FInterpConstantTo(Angle, Target, Dt, 220.f);
	Hinge->SetRelativeRotation(FRotator(0, ClosedYaw + Angle, 0));
	if (bPendingRebuild && FMath::IsNearlyEqual(Angle, Target, 0.1f))
	{
		SettleTime += Dt;
		if (SettleTime > 0.05f)
		{
			bPendingRebuild = false;
			if (ASLSmokeManager* M = ASLSmokeManager::Get(this)) M->OnDoorChanged(this);
		}
	}
}
