#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

// Smoke ray marcher, written as a scene view extension (the report's path R1).
// Layout mirrors what the shipped CS2 shaders imply: every smoke owns a 32^3 voxel grid and one slot of a
// 3D atlas (x stride 34 = 32 + 2 gap texels, 16 slots, 542 x 32 x 32). The atlas stores the fill's arrival
// distance per voxel, so growth, fade and carving are all evaluated in the shader from a few constants.

namespace SLRender
{
	static constexpr int32 GridN = 32;
	static constexpr int32 SlotStride = 34;
	static constexpr int32 MaxSlots = 16;
	static constexpr int32 AtlasW = SlotStride * (MaxSlots - 1) + GridN;	// 542
	static constexpr int32 MaxBullets = 16;
	static constexpr int32 MaxBlasts = 5;
	static constexpr int32 MaxLights = 6;
}

struct FSLRenderSmoke
{
	FVector GridOrigin = FVector::ZeroVector;	// world position of voxel (0,0,0)'s corner
	int32 Slot = 0;
	float Radius = 0.f;			// visible arrival radius now (voxels)
	float Feather = 1.5f;		// edge width (voxels)
	float Density = 1.f;		// overall density multiplier (fade-out)
	int32 Bit = 0;				// bit index used by blast masks
};

struct FSLRenderBullet
{
	FVector Start = FVector::ZeroVector, End = FVector::ZeroVector;
	float Age = 0.f;			// 0..1 over the tunnel lifetime
	float RadiusScale = 1.f;	// k: tunnel radius = base / k
};

struct FSLRenderBlast
{
	FVector Pos = FVector::ZeroVector;
	float Age = 0.f;			// seconds since detonation
	uint32 Mask = 0;			// which smokes (bits) this blast affects
};

struct FSLRenderLight
{
	FVector Pos = FVector::ZeroVector;
	FLinearColor Candela = FLinearColor::Black;	// colour * intensity (cd)
	float RadiusCm = 900.f;
};

struct FSLRenderSettings
{
	bool bEnabled = true;
	int32 ResDiv = 4;			// 1, 2, 4
	int32 Noise = 2;			// 0 none, 1 procedural Worley, 2 noise texture
	int32 Lighting = 0;			// 0 lighting cache, 1 shadow ray per sample, 2 unlit
	int32 Upsample = 2;			// 0 bilinear, 1 depth aware, 2 depth aware + full-res fix-up
	int32 DebugView = 0;		// 0 smoke, 1 arrival heat map, 2 lighting cache only
	bool bGameplayFloor = true;	// noise may only thin the edge band, never the core
	bool bBullets = true, bBlasts = true;
	float StepCm = 10.f;
	int32 MaxSteps = 320;
};

struct FSLRenderFrame
{
	TArray<FSLRenderSmoke> Smokes;
	TArray<FSLRenderBullet> Bullets;
	TArray<FSLRenderBlast> Blasts;
	TArray<FSLRenderLight> Lights;
	FSLRenderSettings Settings;
	FVector SunDir = FVector(0.3, 0.2, 0.93);	// towards the sun
	FLinearColor SunIlluminance = FLinearColor(6.f, 5.8f, 5.4f);
	FLinearColor SkyIlluminance = FLinearColor(1.2f, 1.4f, 1.7f);
	float Time = 0.f;
	float VoxelCm = 50.f;
};

/** GPU times of the last completed frame, in milliseconds. */
struct FSLGpuTimes
{
	float LightMs = 0.f, MarchMs = 0.f, CompositeMs = 0.f;
	int32 LowW = 0, LowH = 0, FixedPixels = -1;
};

class SMOKELABSHADERS_API FSLSmokeRenderer : public FWorldSceneViewExtension
{
public:
	FSLSmokeRenderer(const FAutoRegister& AutoReg, UWorld* InWorld);
	virtual ~FSLSmokeRenderer() override;

	// --- game thread ---
	void SetFrame(const FSLRenderFrame& Frame);
	void UploadSlot(int32 Slot, const TArray<uint8>& Arrival);	// GridN^3 bytes, 255 = no smoke
	void SetWorldOccupancy(const FVector& Origin, const FIntVector& Dim, float VoxelCm, const TArray<uint8>& Solid);
	FSLGpuTimes GetGpuTimes() const;

	// --- ISceneViewExtension ---
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const struct FPostProcessingInputs& Inputs) override;

private:
	struct FRenderState;
	TUniquePtr<FRenderState> RT;	// render-thread state
	mutable FCriticalSection TimesLock;
	FSLGpuTimes Times;
	void SetTimes(const FSLGpuTimes& T) { FScopeLock L(&TimesLock); Times = T; }
};
