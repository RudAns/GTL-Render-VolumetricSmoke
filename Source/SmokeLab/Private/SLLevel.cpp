#include "SLLevel.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Components/PostProcessComponent.h"
#include "Components/PointLightComponent.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "SLSmoke.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	enum EMat { MFloor, MWallOut, MWallIn, MRoof, MWood, MMetal, MMarker, MGrate, MBrick, MGround, MTrim, MCount };
	const TCHAR* MatPaths[MCount] = {
		TEXT("/Game/SmokeLab/Materials/M_Floor.M_Floor"),
		TEXT("/Game/SmokeLab/Materials/M_Concrete.M_Concrete"),
		TEXT("/Game/SmokeLab/Materials/M_Plaster.M_Plaster"),
		TEXT("/Game/SmokeLab/Materials/M_Roof.M_Roof"),
		TEXT("/Game/SmokeLab/Materials/M_Wood.M_Wood"),
		TEXT("/Game/SmokeLab/Materials/M_Metal.M_Metal"),
		TEXT("/Game/SmokeLab/Materials/M_Marker.M_Marker"),
		TEXT("/Game/SmokeLab/Materials/M_Grate.M_Grate"),
		TEXT("/Game/SmokeLab/Materials/M_Brick.M_Brick"),
		TEXT("/Game/SmokeLab/Materials/M_Ground.M_Ground"),
		TEXT("/Game/SmokeLab/Materials/M_Trim.M_Trim"),
	};
	const TCHAR* Fallback[MCount] = {
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"),
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_02.MI_PrototypeGrid_Gray_02"),
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"),
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark"),
		TEXT("/Game/LevelPrototyping/Materials/MI_DefaultColorway.MI_DefaultColorway"),
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_Round.MI_PrototypeGrid_Gray_Round"),
		TEXT("/Game/LevelPrototyping/Materials/MI_DefaultColorway.MI_DefaultColorway"),
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_Round.MI_PrototypeGrid_Gray_Round"),
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_02.MI_PrototypeGrid_Gray_02"),
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"),
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark"),
	};
	UStaticMesh* CubeMesh()
	{
		static UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		return M;
	}
	const float H = 420.f;		// wall height
}

ASLGrate::ASLGrate()
{
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Tags.Add(TEXT("SmokePass"));
}

ASLLevel::ASLLevel()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ASLLevel* ASLLevel::Get(const UObject* Ctx)
{
	UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	if (!W) return nullptr;
	for (TActorIterator<ASLLevel> It(W); It; ++It) return *It;
	return nullptr;
}

void ASLLevel::BeginPlay()
{
	Super::BeginPlay();
	Mats.SetNum(MCount);
	for (int32 I = 0; I < MCount; ++I)
	{
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, MatPaths[I], nullptr, LOAD_NoWarn | LOAD_Quiet);
		Mats[I] = M ? M : LoadObject<UMaterialInterface>(nullptr, Fallback[I]);
	}
	Build();
	Light();
	bBuilt = true;
	if (HasAuthority()) SpawnServerActors();
}

UStaticMeshComponent* ASLLevel::Mesh(UStaticMesh* M, const FTransform& Xf, int32 Mat, bool bCollide)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(M);
	C->SetMobility(EComponentMobility::Static);
	C->SetWorldTransform(Xf);
	if (Mats.IsValidIndex(Mat) && Mats[Mat]) C->SetMaterial(0, Mats[Mat]);
	C->SetCollisionProfileName(bCollide ? TEXT("BlockAll") : TEXT("NoCollision"));
	C->SetupAttachment(RootComponent);
	C->RegisterComponent();
	AddInstanceComponent(C);
	return C;
}

UStaticMeshComponent* ASLLevel::Box(const FVector& Min, const FVector& Max, int32 Mat, bool bCollide)
{
	const FVector Size = Max - Min;
	if (Size.X <= 0.5f || Size.Y <= 0.5f || Size.Z <= 0.5f) return nullptr;
	return Mesh(CubeMesh(), FTransform(FRotator::ZeroRotator, (Min + Max) * 0.5f, Size / 100.f), Mat, bCollide);
}

void ASLLevel::Prop(const TCHAR* Name, const FVector& At, float Yaw, float Scale, bool bCollide)
{
	for (int32 Part = 0; Part < 8; ++Part)
	{
		const FString Path = Part == 0 ? FString::Printf(TEXT("/Game/SmokeLab/Props/SM_%s.SM_%s"), Name, Name)
			: FString::Printf(TEXT("/Game/SmokeLab/Props/SM_%s_%d.SM_%s_%d"), Name, Part, Name, Part);
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!M) break;
		UStaticMeshComponent* C = Mesh(M, FTransform(FRotator(0, Yaw, 0), At, FVector(Scale)), -1, bCollide);
		if (C) C->SetMaterial(0, nullptr);
	}
}

void ASLLevel::Lamp(const FVector& At, float Intensity)
{
	Prop(TEXT("caged_hanging_light"), At, 0.f, 1.f, false);
	UPointLightComponent* L = NewObject<UPointLightComponent>(this);
	L->SetupAttachment(RootComponent);
	L->SetMobility(EComponentMobility::Movable);
	L->SetWorldLocation(At - FVector(0, 0, 30));
	L->IntensityUnits = ELightUnits::Candelas;	// the smoke shader treats the same number as candela
	L->Intensity = Intensity;
	L->LightColor = FLinearColor(1.f, 0.78f, 0.5f).ToFColor(true);
	L->AttenuationRadius = 900.f;
	L->SourceRadius = 6.f;
	L->RegisterComponent();
	Lamps.Add(L);
}

void ASLLevel::Wall(const FVector2D& A, const FVector2D& B, float Thick, float Z0, float Z1, int32 Mat, const TArray<FVector4>& Gaps)
{
	// axis-aligned wall from A to B; gaps are (from, to, bottom, top) along the wall
	const bool bX = FMath::Abs(B.X - A.X) >= FMath::Abs(B.Y - A.Y);
	const float L0 = bX ? FMath::Min(A.X, B.X) : FMath::Min(A.Y, B.Y);
	const float L1 = bX ? FMath::Max(A.X, B.X) : FMath::Max(A.Y, B.Y);
	const float C = bX ? A.Y : A.X;
	auto Seg = [&](float S0, float S1, float ZA, float ZB)
	{
		if (S1 - S0 < 1.f || ZB - ZA < 1.f) return;
		if (bX) Box(FVector(S0, C - Thick * 0.5f, ZA), FVector(S1, C + Thick * 0.5f, ZB), Mat);
		else Box(FVector(C - Thick * 0.5f, S0, ZA), FVector(C + Thick * 0.5f, S1, ZB), Mat);
	};
	TArray<FVector4> Sorted = Gaps;
	Sorted.Sort([](const FVector4& P, const FVector4& Q) { return P.X < Q.X; });
	float Cur = L0;
	for (const FVector4& G : Sorted)
	{
		Seg(Cur, G.X, Z0, Z1);
		Seg(G.X, G.Y, Z0, G.Z);	// sill
		Seg(G.X, G.Y, G.W, Z1);	// lintel
		Cur = G.Y;
	}
	Seg(Cur, L1, Z0, Z1);
}

void ASLLevel::Build()
{
	// ground and courtyard
	Box(FVector(-600, -600, -40), FVector(5400, 3800, 0), MGround);
	Box(FVector(1400, 560, -2), FVector(4800, 1700, 0.5f), MFloor, false);		// indoor floors (visual)

	// outer frame
	Wall({ -20, -20 }, { 4820, -20 }, 40, 0, 600, MBrick);
	Wall({ -20, 3220 }, { 4820, 3220 }, 40, 0, 600, MBrick);
	Wall({ -20, -20 }, { -20, 3220 }, 40, 0, 600, MBrick);
	Wall({ 4820, -20 }, { 4820, 3220 }, 40, 0, 600, MBrick);

	// --- long corridor: x 1400..3400, y 600..1000, roofed; a door at x 2400, a grate in the north wall
	Wall({ 1400, 580 }, { 3400, 580 }, 40, 0, H, MWallOut);
	Wall({ 1400, 1020 }, { 3400, 1020 }, 40, 0, H, MWallIn, { FVector4(2800, 3000, 40, 260) });
	Box(FVector(1380, 560, H), FVector(3420, 1040, H + 40), MRoof);
	Wall({ 2400, 600 }, { 2400, 1000 }, 40, 0, H, MWallIn, { FVector4(700, 900, 0, 270) });
	Box(FVector(2380, 560, 270), FVector(2420, 1040, 300), MTrim);
	S.CorridorMouth = FVector(1500, 800, 0);
	S.CorridorDoor = FVector(2400, 800, 0);
	S.GrateWindow = FVector(2900, 1020, 150);
	// grate (its own actor so the voxeliser can tell it lets smoke through)
	{
		FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASLGrate* G = GetWorld()->SpawnActor<ASLGrate>(FVector(2900, 1020, 150), FRotator::ZeroRotator, P);
		if (G)
		{
			G->Mesh->SetStaticMesh(CubeMesh());
			G->Mesh->SetWorldScale3D(FVector(2.0f, 0.06f, 2.2f));
			if (Mats[MGrate]) G->Mesh->SetMaterial(0, Mats[MGrate]);
		}
	}

	// --- connector room behind the grate: x 2600..3200, y 1040..1700
	Wall({ 2600, 1040 }, { 2600, 1700 }, 40, 0, H, MWallIn);
	Wall({ 3200, 1040 }, { 3200, 1700 }, 40, 0, H, MWallIn);
	Box(FVector(2580, 1020, H), FVector(3220, 1720, H + 40), MRoof);

	// --- mid lane: x 1400..4800, y 1700..2300, open sky
	Wall({ 1400, 1700 }, { 3400, 1700 }, 40, 0, H, MWallOut, { FVector4(2800, 3000, 0, 300) });
	Wall({ 1400, 2320 }, { 4800, 2320 }, 40, 0, 360, MBrick);
	Box(FVector(4380, 1820, 0), FVector(4800, 2240, 60), MWood);		// sentry stand at mid end
	S.MidLane = FVector(2000, 2000, 0);

	// --- bomb site building: x 3400..4800, y 200..1700
	Wall({ 3400, 200 }, { 3400, 1700 }, 40, 0, 460, MWallOut, { FVector4(600, 1000, 0, 280) });
	Wall({ 3400, 200 }, { 4800, 200 }, 40, 0, 460, MWallOut);
	Wall({ 3400, 1700 }, { 4800, 1700 }, 40, 0, 460, MWallOut, { FVector4(3850, 4250, 0, 320), FVector4(4500, 4700, 130, 250) });
	Box(FVector(3380, 180, 460), FVector(4820, 1720, 500), MRoof);
	S.SiteWindow = FVector(4600, 1700, 190);
	// corridor opens into the site (the corridor's own walls end at x 3400)
	// platform with stairs along the east side
	Box(FVector(4400, 1150, 0), FVector(4780, 1680, 160), MWood);
	for (int32 K = 0; K < 4; ++K)
		Box(FVector(4400 - (K + 1) * 45.f, 1150, 0), FVector(4400 - K * 45.f, 1450, 160.f - (K + 1) * 40.f), MWood);
	S.Stairs = FVector(4250, 1300, 0);
	// plant zone with a painted marker
	S.PlantZone = FBox(FVector(4000, 350, 0), FVector(4500, 900, 200));
	Box(FVector(4000, 350, 0.5f), FVector(4500, 900, 2.5f), MMarker, false);
	{
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
		T->SetText(FText::FromString(TEXT("A")));
		T->SetWorldSize(220.f);
		T->SetTextRenderColor(FColor(255, 150, 40));
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetWorldLocationAndRotation(FVector(4779, 620, 260), FRotator(0, 180, 0));
		T->SetupAttachment(RootComponent);
		T->RegisterComponent();
	}
	// cover
	Box(FVector(4150, 1000, 0), FVector(4290, 1140, 120), MWood);
	Box(FVector(3700, 300, 0), FVector(3840, 440, 120), MWood);
	Box(FVector(3700, 300, 120), FVector(3800, 400, 220), MWood);
	Box(FVector(4600, 650, 0), FVector(4720, 900, 110), MMetal);

	// --- courtyard dressing: a free-standing thin wall (rule A leaks through it) and crates
	Box(FVector(800, 1850, 0), FVector(830, 2950, 400), MBrick);
	S.ThinWall = FVector(760, 2350, 0);
	Box(FVector(1000, 400, 0), FVector(1140, 540, 140), MWood);
	Box(FVector(1100, 1300, 0), FVector(1240, 1440, 140), MWood);
	Box(FVector(1100, 1300, 140), FVector(1200, 1400, 240), MWood);
	Box(FVector(300, 1600, 0), FVector(420, 1720, 110), MMetal);
	Box(FVector(1900, 1850, 0), FVector(2040, 1990, 130), MWood);
	Box(FVector(3200, 2050, 0), FVector(3340, 2190, 130), MWood);

	// --- props (Poly Haven CC0)
	Prop(TEXT("concrete_road_barrier"), FVector(980, 1150, 0), 0.f);
	Prop(TEXT("concrete_road_barrier"), FVector(320, 820, 0), 90.f);
	Prop(TEXT("old_military_crate"), FVector(1700, 2150, 0), 20.f);
	Prop(TEXT("Barrel_01"), FVector(360, 1500, 0), 0.f);
	Prop(TEXT("barrel_03"), FVector(430, 1450, 0), 40.f);
	Prop(TEXT("portable_generator"), FVector(1320, 380, 0), 80.f);
	Prop(TEXT("power_box_01"), FVector(1395, 1250, 120), -90.f, 1.f, false);
	Prop(TEXT("plastic_crate_01"), FVector(2600, 2200, 0), 10.f);
	Prop(TEXT("plastic_crate_01"), FVector(2610, 2200, 30), 35.f);
	Prop(TEXT("concrete_road_barrier"), FVector(3600, 2050, 0), 90.f);
	Prop(TEXT("Barrel_01"), FVector(2900, 1600, 0), 0.f);
	Prop(TEXT("metal_jerrycan"), FVector(2980, 1580, 0), 30.f);
	Prop(TEXT("old_military_crate"), FVector(4300, 450, 0), 0.f);
	Prop(TEXT("ammo_box"), FVector(4560, 1300, 160), 15.f);
	Prop(TEXT("barrel_03"), FVector(3550, 1450, 0), 0.f);
	Prop(TEXT("Barrel_01"), FVector(3620, 1500, 0), 50.f);
	Prop(TEXT("cardboard_box_01"), FVector(3560, 620, 0), 10.f);
	Prop(TEXT("industrial_wall_lamp"), FVector(3420, 1100, 300), 0.f, 1.f, false);
	Lamp(FVector(1900, 800, 405), 18.f);
	Lamp(FVector(2900, 800, 405), 18.f);
	Lamp(FVector(3800, 900, 455), 22.f);
	Lamp(FVector(4300, 1200, 455), 22.f);
	Lamp(FVector(2900, 1370, 405), 14.f);

	// sentries: (location, facing yaw)
	S.Sentries = {
		FTransform(FRotator(0, 180, 0), FVector(4640, 780, 100)),		// watches the corridor exit into the site
		FTransform(FRotator(0, -150, 0), FVector(4620, 1520, 260)),		// on the platform, watches the arch
		FTransform(FRotator(0, 180, 0), FVector(4640, 2020, 160)),		// mid end, watches the mid lane
	};
	S.PlayerStart = FVector(500, 500, 100);
	S.PlayerStart2 = FVector(500, 1000, 100);
}

void ASLLevel::Light()
{
	// Every property is set before RegisterComponent: a sky light whose real-time capture is switched on after
	// registration never captures anything, and every surface out of the sun stays black.
	SunLight = NewObject<UDirectionalLightComponent>(this);
	SunLight->SetupAttachment(RootComponent);
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetWorldRotation(FRotator(-42.f, 35.f, 0.f));
	SunLight->Intensity = 6.f;
	SunLight->LightColor = FLinearColor(1.f, 0.95f, 0.88f).ToFColor(true);
	SunLight->bAtmosphereSunLight = true;
	SunLight->CastShadows = true;
	SunLight->LightSourceAngle = 1.0f;
	SunLight->RegisterComponent();
	USkyAtmosphereComponent* Atm = NewObject<USkyAtmosphereComponent>(this);
	Atm->SetupAttachment(RootComponent);
	Atm->RegisterComponent();
	USkyLightComponent* Sky = NewObject<USkyLightComponent>(this);
	Sky->SetupAttachment(RootComponent);
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->bRealTimeCapture = true;
	Sky->Intensity = 2.5f;
	Sky->bLowerHemisphereIsBlack = false;
	Sky->LowerHemisphereColor = FLinearColor(0.16f, 0.15f, 0.13f);
	Sky->RegisterComponent();
	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(RootComponent);
	Fog->FogDensity = 0.004f;
	Fog->FogHeightFalloff = 0.4f;
	Fog->RegisterComponent();
	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(this);
	PP->SetupAttachment(RootComponent);
	PP->bUnbound = true;
	PP->Settings.bOverride_AutoExposureMethod = true;
	PP->Settings.AutoExposureMethod = AEM_Manual;
	PP->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP->Settings.AutoExposureApplyPhysicalCameraExposure = false;
	PP->Settings.bOverride_AutoExposureBias = true;
	PP->Settings.AutoExposureBias = 0.f;
	PP->RegisterComponent();
}

void ASLLevel::SpawnServerActors()
{
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ASLDoor* D = GetWorld()->SpawnActor<ASLDoor>(FVector(2400, 700, 0), FRotator::ZeroRotator, P))
	{
		D->Setup(FVector(2400, 700, 0), 200.f, 270.f, 90.f, Mats.IsValidIndex(MWood) ? Mats[MWood].Get() : nullptr);
		Door = D;
	}
}
