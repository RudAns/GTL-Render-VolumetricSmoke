#include "SLRender.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "FXRenderingUtils.h"
#include "GlobalShader.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SceneRenderTargetParameters.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"

// ---------------------------------------------------------------- shaders

BEGIN_SHADER_PARAMETER_STRUCT(FSLCommonParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER(uint32, NumSmokes)
	SHADER_PARAMETER_ARRAY(FVector4f, SmokeA, [SLRender::MaxSlots])
	SHADER_PARAMETER_ARRAY(FVector4f, SmokeB, [SLRender::MaxSlots])
	SHADER_PARAMETER(uint32, NumBullets)
	SHADER_PARAMETER_ARRAY(FVector4f, BulletS, [SLRender::MaxBullets])
	SHADER_PARAMETER_ARRAY(FVector4f, BulletE, [SLRender::MaxBullets])
	SHADER_PARAMETER(uint32, NumBlasts)
	SHADER_PARAMETER_ARRAY(FVector4f, BlastP, [SLRender::MaxBlasts])
	SHADER_PARAMETER_ARRAY(FVector4f, BlastM, [SLRender::MaxBlasts])
	SHADER_PARAMETER(uint32, NumLights)
	SHADER_PARAMETER_ARRAY(FVector4f, LightP, [SLRender::MaxLights])
	SHADER_PARAMETER_ARRAY(FVector4f, LightC, [SLRender::MaxLights])
	SHADER_PARAMETER(float, VoxelCm)
	SHADER_PARAMETER(float, Time)
	SHADER_PARAMETER(FVector3f, PreViewT)
	SHADER_PARAMETER(uint32, NoiseMode)
	SHADER_PARAMETER(uint32, LightMode)
	SHADER_PARAMETER(uint32, DebugView)
	SHADER_PARAMETER(uint32, GameplayFloor)
	SHADER_PARAMETER(FVector3f, SunDir)
	SHADER_PARAMETER(FVector3f, SunIlluminance)
	SHADER_PARAMETER(FVector3f, SkyIlluminance)
	SHADER_PARAMETER(FVector3f, WorldOccOrigin)
	SHADER_PARAMETER(FVector3f, WorldOccDim)
	SHADER_PARAMETER(float, StepCm)
	SHADER_PARAMETER(uint32, MaxSteps)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, Atlas)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, LightCache)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float2>, NoiseTex)
	SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float>, WorldOcc)
	SHADER_PARAMETER_SAMPLER(SamplerState, LinearClamp)
	SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrap)
END_SHADER_PARAMETER_STRUCT()

class FSLLightCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSLLightCS);
	SHADER_USE_PARAMETER_STRUCT(FSLLightCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSLCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWLightCache)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FSLLightCS, "/SmokeLab/SmokeLab.usf", "LightCS", SF_Compute);

class FSLMarchCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSLMarchCS);
	SHADER_USE_PARAMETER_STRUCT(FSLMarchCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSLCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSmokeColor)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWSmokeDepth)
		SHADER_PARAMETER(FUintVector2, LowSize)
		SHADER_PARAMETER(uint32, ResDiv)
		SHADER_PARAMETER(FIntPoint, ViewMin)
		SHADER_PARAMETER(FIntPoint, ViewMax)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FSLMarchCS, "/SmokeLab/SmokeLab.usf", "MarchCS", SF_Compute);

class FSLCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSLCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSLCompositePS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSLCommonParameters, Common)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, SmokeColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SmokeDepth)
		SHADER_PARAMETER(FUintVector2, LowSize)
		SHADER_PARAMETER(uint32, ResDiv)
		SHADER_PARAMETER(FIntPoint, ViewMin)
		SHADER_PARAMETER(FIntPoint, ViewMax)
		SHADER_PARAMETER(uint32, UpsampleMode)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P) { return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FSLCompositePS, "/SmokeLab/SmokeLab.usf", "CompositePS", SF_Pixel);

// ---------------------------------------------------------------- render-thread state

struct FSLSmokeRenderer::FRenderState
{
	FTextureRHIRef Atlas, LightCache, Noise, WorldOcc;
	FVector WorldOrigin = FVector::ZeroVector;
	FIntVector WorldDim = FIntVector(1, 1, 1);
	FSLRenderFrame Frame;
	// GPU timers: 4 timestamps per frame, results read a few frames later
	FRenderQueryPoolRHIRef QueryPool;
	struct FTimerSet { FRHIPooledRenderQuery Q[4]; bool bIssued = false; int32 LowW = 0, LowH = 0; };
	FTimerSet Timers[4];
	int32 TimerFrame = 0;
};

namespace
{
	// Tileable two-octave Worley noise (R: 4 cells per tile, G: 10 cells per tile), stored as 1 - F1 distance.
	TArray<uint8> MakeNoise(int32 S)
	{
		TArray<uint8> Out;
		Out.SetNumUninitialized(S * S * S * 2);
		auto Octave = [S](int32 Cells, int32 Seed, TArray<FVector3f>& Pts)
		{
			FRandomStream R(Seed);
			Pts.SetNum(Cells * Cells * Cells);
			for (FVector3f& P : Pts) P = FVector3f(R.FRand(), R.FRand(), R.FRand());
		};
		TArray<FVector3f> PA, PB;
		Octave(4, 11, PA);
		Octave(10, 23, PB);
		auto Eval = [](const TArray<FVector3f>& Pts, int32 Cells, FVector3f P)
		{
			const FVector3f C(FMath::FloorToFloat(P.X), FMath::FloorToFloat(P.Y), FMath::FloorToFloat(P.Z));
			float Best = 9.f;
			for (int32 Z = -1; Z <= 1; ++Z) for (int32 Y = -1; Y <= 1; ++Y) for (int32 X = -1; X <= 1; ++X)
			{
				const int32 CX = int32(C.X) + X, CY = int32(C.Y) + Y, CZ = int32(C.Z) + Z;
				const int32 WX = (CX % Cells + Cells) % Cells, WY = (CY % Cells + Cells) % Cells, WZ = (CZ % Cells + Cells) % Cells;
				const FVector3f F = FVector3f(CX, CY, CZ) + Pts[(WZ * Cells + WY) * Cells + WX] - P;
				Best = FMath::Min(Best, F.SizeSquared());
			}
			return FMath::Clamp(1.f - FMath::Sqrt(Best), 0.f, 1.f);
		};
		for (int32 Z = 0; Z < S; ++Z) for (int32 Y = 0; Y < S; ++Y) for (int32 X = 0; X < S; ++X)
		{
			const FVector3f U = (FVector3f(X, Y, Z) + 0.5f) / float(S);
			const int32 I = ((Z * S + Y) * S + X) * 2;
			Out[I + 0] = (uint8)FMath::RoundToInt(Eval(PA, 4, U * 4.f) * 255.f);
			Out[I + 1] = (uint8)FMath::RoundToInt(Eval(PB, 10, U * 10.f) * 255.f);
		}
		return Out;
	}

	FTextureRHIRef Create3D(FRHICommandListImmediate& RHICmdList, const TCHAR* Name, FIntVector Size, EPixelFormat Format, bool bUAV)
	{
		FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create3D(Name, Size.X, Size.Y, Size.Z, Format)
			.SetFlags(ETextureCreateFlags::ShaderResource | (bUAV ? ETextureCreateFlags::UAV : ETextureCreateFlags::None))
			.SetInitialState(ERHIAccess::SRVMask);
		return RHICmdList.CreateTexture(Desc);
	}

	void Upload3D(FRHICommandListImmediate& RHICmdList, FRHITexture* Tex, FIntVector Dst, FIntVector Size, int32 BytesPerTexel, const TArray<uint8>& Data)
	{
		const FUpdateTextureRegion3D Region(Dst.X, Dst.Y, Dst.Z, 0, 0, 0, Size.X, Size.Y, Size.Z);
		RHICmdList.UpdateTexture3D(Tex, 0, Region, Size.X * BytesPerTexel, Size.X * Size.Y * BytesPerTexel, Data.GetData());
	}
}

FSLSmokeRenderer::FSLSmokeRenderer(const FAutoRegister& AutoReg, UWorld* InWorld)
	: FWorldSceneViewExtension(AutoReg, InWorld)
	, RT(MakeUnique<FRenderState>())
{
	TArray<uint8> NoiseData = MakeNoise(64);
	FRenderState* S = RT.Get();
	ENQUEUE_RENDER_COMMAND(SLCreate)([S, NoiseData = MoveTemp(NoiseData)](FRHICommandListImmediate& RHICmdList)
	{
		using namespace SLRender;
		S->Atlas = Create3D(RHICmdList, TEXT("SLAtlas"), FIntVector(AtlasW, GridN, GridN), PF_R8, false);
		S->LightCache = Create3D(RHICmdList, TEXT("SLLightCache"), FIntVector(AtlasW, GridN, GridN), PF_FloatRGBA, true);
		S->Noise = Create3D(RHICmdList, TEXT("SLNoise"), FIntVector(64, 64, 64), PF_R8G8, false);
		Upload3D(RHICmdList, S->Noise, FIntVector::ZeroValue, FIntVector(64, 64, 64), 2, NoiseData);
		// empty atlas
		TArray<uint8> Empty; Empty.Init(255, AtlasW * GridN * GridN);
		Upload3D(RHICmdList, S->Atlas, FIntVector::ZeroValue, FIntVector(AtlasW, GridN, GridN), 1, Empty);
		S->WorldOcc = Create3D(RHICmdList, TEXT("SLWorldOcc"), FIntVector(1, 1, 1), PF_R8, false);
		TArray<uint8> Zero; Zero.Init(0, 1);
		Upload3D(RHICmdList, S->WorldOcc, FIntVector::ZeroValue, FIntVector(1, 1, 1), 1, Zero);
		S->QueryPool = RHICreateRenderQueryPool(RQT_AbsoluteTime, 64);
	});
}

FSLSmokeRenderer::~FSLSmokeRenderer()
{
	// The render thread may still use the state: hand it over and free it there.
	FRenderState* S = RT.Release();
	ENQUEUE_RENDER_COMMAND(SLFree)([S](FRHICommandListImmediate&) { delete S; });
}

void FSLSmokeRenderer::SetFrame(const FSLRenderFrame& Frame)
{
	FRenderState* S = RT.Get();
	ENQUEUE_RENDER_COMMAND(SLFrame)([S, Frame](FRHICommandListImmediate&) { S->Frame = Frame; });
}

void FSLSmokeRenderer::UploadSlot(int32 Slot, const TArray<uint8>& Arrival)
{
	using namespace SLRender;
	if (Slot < 0 || Slot >= MaxSlots || Arrival.Num() != GridN * GridN * GridN) return;
	FRenderState* S = RT.Get();
	ENQUEUE_RENDER_COMMAND(SLUpload)([S, Slot, Arrival](FRHICommandListImmediate& RHICmdList)
	{
		if (S->Atlas) Upload3D(RHICmdList, S->Atlas, FIntVector(Slot * SlotStride, 0, 0), FIntVector(GridN, GridN, GridN), 1, Arrival);
	});
}

void FSLSmokeRenderer::SetWorldOccupancy(const FVector& Origin, const FIntVector& Dim, float VoxelCm, const TArray<uint8>& Solid)
{
	if (Solid.Num() != Dim.X * Dim.Y * Dim.Z || Solid.Num() == 0) return;
	FRenderState* S = RT.Get();
	ENQUEUE_RENDER_COMMAND(SLWorld)([S, Origin, Dim, Solid](FRHICommandListImmediate& RHICmdList)
	{
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Solid.Num());
		for (int32 I = 0; I < Solid.Num(); ++I) Bytes[I] = Solid[I] ? 255 : 0;
		S->WorldOcc = Create3D(RHICmdList, TEXT("SLWorldOcc"), Dim, PF_R8, false);
		Upload3D(RHICmdList, S->WorldOcc, FIntVector::ZeroValue, Dim, 1, Bytes);
		S->WorldOrigin = Origin;
		S->WorldDim = Dim;
	});
}

FSLGpuTimes FSLSmokeRenderer::GetGpuTimes() const
{
	FScopeLock L(&TimesLock);
	return Times;
}

void FSLSmokeRenderer::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	using namespace SLRender;
	FRenderState& S = *RT;
	if (!S.Atlas || !S.LightCache) return;
	const FSLRenderFrame& F = S.Frame;

	// read back timers issued a few frames ago
	{
		FRenderState::FTimerSet& Old = S.Timers[(S.TimerFrame + 1) % 4];
		if (Old.bIssued)
		{
			uint64 T[4];
			bool bOk = true;
			for (int32 K = 0; K < 4 && bOk; ++K) bOk = RHIGetRenderQueryResult(Old.Q[K].GetQuery(), T[K], false);
			if (bOk)
			{
				FSLGpuTimes G;
				G.LightMs = (T[1] - T[0]) * 0.001f;
				G.MarchMs = (T[2] - T[1]) * 0.001f;
				G.CompositeMs = (T[3] - T[2]) * 0.001f;
				G.LowW = Old.LowW; G.LowH = Old.LowH;
				SetTimes(G);
			}
			for (int32 K = 0; K < 4; ++K) Old.Q[K].ReleaseQuery();
			Old.bIssued = false;
		}
	}

	if (!F.Settings.bEnabled || F.Smokes.Num() == 0 || !Inputs.SceneTextures) return;
	Inputs.Validate();

	const FIntRect ViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(View);
	FRDGTextureRef SceneColor = (*Inputs.SceneTextures)->SceneColorTexture;
	FRDGTextureRef SceneDepthTex = (*Inputs.SceneTextures)->SceneDepthTexture;
	if (!SceneColor || !SceneDepthTex) return;

	const FVector PreView = View.ViewMatrices.GetPreViewTranslation();
	const int32 ResDiv = FMath::Clamp(F.Settings.ResDiv, 1, 4);
	const FIntPoint Low(FMath::DivideAndRoundUp(ViewRect.Width(), ResDiv), FMath::DivideAndRoundUp(ViewRect.Height(), ResDiv));

	FGlobalShaderMap* Map = GetGlobalShaderMap(View.GetFeatureLevel());
	RDG_EVENT_SCOPE(GraphBuilder, "SmokeLab %dx%d", Low.X, Low.Y);

	FRDGTextureRef RAtlas = RegisterExternalTexture(GraphBuilder, S.Atlas, TEXT("SLAtlas"));
	FRDGTextureRef RCache = RegisterExternalTexture(GraphBuilder, S.LightCache, TEXT("SLLightCache"));
	FRDGTextureRef RNoise = RegisterExternalTexture(GraphBuilder, S.Noise, TEXT("SLNoise"));
	FRDGTextureRef RWorld = RegisterExternalTexture(GraphBuilder, S.WorldOcc, TEXT("SLWorldOcc"));

	auto FillCommon = [&](FSLCommonParameters& C, bool bCacheReadable)
	{
		C.View = View.ViewUniformBuffer;
		C.NumSmokes = FMath::Min(F.Smokes.Num(), MaxSlots);
		for (int32 I = 0; I < (int32)C.NumSmokes; ++I)
		{
			const FSLRenderSmoke& Sm = F.Smokes[I];
			C.SmokeA[I] = FVector4f(FVector3f(Sm.GridOrigin + PreView), float(Sm.Slot));
			C.SmokeB[I] = FVector4f(Sm.Radius, Sm.Feather, Sm.Density, float(Sm.Bit));
		}
		C.NumBullets = F.Settings.bBullets ? FMath::Min(F.Bullets.Num(), MaxBullets) : 0;
		for (int32 I = 0; I < (int32)C.NumBullets; ++I)
		{
			C.BulletS[I] = FVector4f(FVector3f(F.Bullets[I].Start + PreView), F.Bullets[I].Age);
			C.BulletE[I] = FVector4f(FVector3f(F.Bullets[I].End + PreView), F.Bullets[I].RadiusScale);
		}
		C.NumBlasts = F.Settings.bBlasts ? FMath::Min(F.Blasts.Num(), MaxBlasts) : 0;
		for (int32 I = 0; I < (int32)C.NumBlasts; ++I)
		{
			C.BlastP[I] = FVector4f(FVector3f(F.Blasts[I].Pos + PreView), F.Blasts[I].Age);
			C.BlastM[I] = FVector4f(float(F.Blasts[I].Mask), 0, 0, 0);
		}
		C.NumLights = FMath::Min(F.Lights.Num(), MaxLights);
		for (int32 I = 0; I < (int32)C.NumLights; ++I)
		{
			C.LightP[I] = FVector4f(FVector3f(F.Lights[I].Pos + PreView), F.Lights[I].RadiusCm);
			C.LightC[I] = FVector4f(F.Lights[I].Candela.R, F.Lights[I].Candela.G, F.Lights[I].Candela.B, 0);
		}
		C.VoxelCm = F.VoxelCm;
		C.Time = F.Time;
		C.PreViewT = FVector3f(PreView);
		C.NoiseMode = (uint32)F.Settings.Noise;
		C.LightMode = (uint32)F.Settings.Lighting;
		C.DebugView = (uint32)F.Settings.DebugView;
		C.GameplayFloor = F.Settings.bGameplayFloor ? 1u : 0u;
		C.SunDir = FVector3f(F.SunDir.GetSafeNormal());
		C.SunIlluminance = FVector3f(F.SunIlluminance.R, F.SunIlluminance.G, F.SunIlluminance.B);
		C.SkyIlluminance = FVector3f(F.SkyIlluminance.R, F.SkyIlluminance.G, F.SkyIlluminance.B);
		C.WorldOccOrigin = FVector3f(S.WorldOrigin + PreView);
		C.WorldOccDim = FVector3f(S.WorldDim);
		C.StepCm = FMath::Max(F.Settings.StepCm, 2.f);
		C.MaxSteps = (uint32)F.Settings.MaxSteps;
		C.Atlas = RAtlas;
		C.LightCache = bCacheReadable ? RCache : GSystemTextures.GetVolumetricBlackDummy(GraphBuilder);
		C.NoiseTex = RNoise;
		C.WorldOcc = RWorld;
		C.LinearClamp = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		C.LinearWrap = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
	};

	FRenderState::FTimerSet& TS = S.Timers[S.TimerFrame % 4];
	const bool bTime = S.QueryPool.IsValid() && !TS.bIssued;
	if (bTime)
	{
		for (int32 K = 0; K < 4; ++K) TS.Q[K] = S.QueryPool->AllocateQuery();
		TS.LowW = Low.X; TS.LowH = Low.Y;
	}
	auto Stamp = [&](int32 K)
	{
		if (!bTime) return;
		FRHIRenderQuery* Q = TS.Q[K].GetQuery();
		GraphBuilder.AddPass(RDG_EVENT_NAME("SLTimer%d", K), ERDGPassFlags::NeverCull, [Q](FRHICommandListImmediate& RHICmdList) { RHICmdList.EndRenderQuery(Q); });
	};

	Stamp(0);
	// 1. lighting cache: every voxel of every active smoke, one thread each
	{
		FSLLightCS::FParameters* P = GraphBuilder.AllocParameters<FSLLightCS::FParameters>();
		FillCommon(P->Common, false);
		P->RWLightCache = GraphBuilder.CreateUAV(RCache);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SmokeLightCache"), TShaderMapRef<FSLLightCS>(Map), P,
			FIntVector(GridN / 4, GridN / 4, (GridN * P->Common.NumSmokes) / 4));
	}
	Stamp(1);
	// 2. ray march at the reduced resolution
	const FRDGTextureDesc ColDesc = FRDGTextureDesc::Create2D(Low, PF_FloatRGBA, FClearValueBinding::Transparent, TexCreate_ShaderResource | TexCreate_UAV);
	const FRDGTextureDesc DepDesc = FRDGTextureDesc::Create2D(Low, PF_R32_FLOAT, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV);
	FRDGTextureRef SmokeCol = GraphBuilder.CreateTexture(ColDesc, TEXT("SLSmokeColor"));
	FRDGTextureRef SmokeDep = GraphBuilder.CreateTexture(DepDesc, TEXT("SLSmokeDepth"));
	{
		FSLMarchCS::FParameters* P = GraphBuilder.AllocParameters<FSLMarchCS::FParameters>();
		FillCommon(P->Common, true);
		P->SceneDepth = SceneDepthTex;
		P->RWSmokeColor = GraphBuilder.CreateUAV(SmokeCol);
		P->RWSmokeDepth = GraphBuilder.CreateUAV(SmokeDep);
		P->LowSize = FUintVector2(Low.X, Low.Y);
		P->ResDiv = ResDiv;
		P->ViewMin = ViewRect.Min;
		P->ViewMax = ViewRect.Max;
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SmokeMarch"), TShaderMapRef<FSLMarchCS>(Map), P,
			FComputeShaderUtils::GetGroupCount(Low, 8));
	}
	Stamp(2);
	// 3. upsample and composite over scene colour (premultiplied alpha)
	{
		FSLCompositePS::FParameters* P = GraphBuilder.AllocParameters<FSLCompositePS::FParameters>();
		FillCommon(P->Common, true);
		P->SceneDepth = SceneDepthTex;
		P->SmokeColor = SmokeCol;
		P->SmokeDepth = SmokeDep;
		P->LowSize = FUintVector2(Low.X, Low.Y);
		P->ResDiv = ResDiv;
		P->ViewMin = ViewRect.Min;
		P->ViewMax = ViewRect.Max;
		P->UpsampleMode = (uint32)F.Settings.Upsample;
		P->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
		FPixelShaderUtils::AddFullscreenPass(GraphBuilder, Map, RDG_EVENT_NAME("SmokeComposite"), TShaderMapRef<FSLCompositePS>(Map), P, ViewRect,
			TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI());
	}
	Stamp(3);
	if (bTime) TS.bIssued = true;
	++S.TimerFrame;
}

// ---------------------------------------------------------------- module

class FSmokeLabShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		AddShaderSourceDirectoryMapping(TEXT("/SmokeLab"), FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), TEXT("Shaders")));
	}
};
IMPLEMENT_MODULE(FSmokeLabShadersModule, SmokeLabShaders);
