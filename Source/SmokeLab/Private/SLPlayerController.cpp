#include "SLPlayerController.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SLBot.h"
#include "SLCharacter.h"
#include "SLGame.h"
#include "SLHud.h"
#include "SLSmoke.h"
#include "SLSmokeManager.h"

ASLPlayerController::ASLPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASLPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	FParse::Value(FCommandLine::Get(), TEXT("-SLCapture="), CaptureDir);
	FParse::Value(FCommandLine::Get(), TEXT("-SLCaptureRole="), CaptureRole);
	Hud = CreateWidget<USLHud>(this, USLHud::StaticClass());
	if (Hud) Hud->AddToViewport();
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	// Window title and -SLSide=L|R placement (Run2P puts host and client side by side). The engine centres every
	// new window and names it only when first shown, so do both a moment later.
	FParse::Value(FCommandLine::Get(), TEXT("-SLSide="), WindowSide);
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, this, &ASLPlayerController::PlaceWindow, 0.5f, false);
	}
	// Started to join a host but the connection failed (host not up yet): the engine falls back to a local copy
	// of the map. Keep retrying instead of silently looking like a second host.
	if (FParse::Value(FCommandLine::Get(), TEXT("-SLJoin="), JoinAddress) && GetNetMode() != NM_Client)
		GetWorldTimerManager().SetTimer(JoinTimer, this, &ASLPlayerController::RetryJoin, 3.f, true, 3.f);
}

void ASLPlayerController::PlaceWindow()
{
	if (!FSlateApplication::IsInitialized() || !GEngine || !GEngine->GameViewport) return;
	TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow();
	if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) return;
	// name the role in the title bar so two windows on one screen are never mistaken for each other
	const ENetMode Mode = GetNetMode();
	Window->SetTitle(FText::FromString(Mode == NM_Client ? TEXT("烟雾演练 · 客户端")
		: (Mode == NM_ListenServer ? TEXT("烟雾演练 · 主机") : TEXT("烟雾演练 · 单机"))));
	if (WindowSide.IsEmpty()) return;
	FDisplayMetrics Display;
	FSlateApplication::Get().GetInitialDisplayMetrics(Display);
	const FPlatformRect& WA = Display.PrimaryDisplayWorkAreaRect;
	const float W = (WA.Right - WA.Left) * 0.5f;
	const float H = FMath::Min<float>(WA.Bottom - WA.Top, W * 0.66f);
	// ReshapeWindow takes the client area, so leave room for the frame and title bar
	const float Border = Window->GetNativeWindow()->GetWindowBorderSize();
	const float Title = Window->GetNativeWindow()->GetWindowTitleBarSize();
	const float X = WA.Left + (WindowSide.StartsWith(TEXT("R")) ? W : 0.f);
	Window->ReshapeWindow(FVector2D(X + Border, WA.Top + Title + Border), FVector2D(W - 2.f * Border, H - Title - 2.f * Border));
}

void ASLPlayerController::RetryJoin()
{
	if (GetNetMode() == NM_Client) { GetWorldTimerManager().ClearTimer(JoinTimer); return; }
	ConsoleCommand(FString::Printf(TEXT("open %s"), *JoinAddress));
}

void ASLPlayerController::OnPossess(APawn* P)
{
	Super::OnPossess(P);
}

ASLCharacter* ASLPlayerController::GetSoldier() const { return Cast<ASLCharacter>(GetPawn()); }
ASLSmokeManager* ASLPlayerController::GetManager() const { return ASLSmokeManager::Get(this); }

void ASLPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ASLPlayerController::OnFirePressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ASLPlayerController::OnFireReleased);
	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &ASLPlayerController::OnUsePressed);
	InputComponent->BindKey(EKeys::E, IE_Released, this, &ASLPlayerController::OnUseReleased);
	InputComponent->BindKey(EKeys::G, IE_Pressed, this, &ASLPlayerController::OnSmoke);
	InputComponent->BindKey(EKeys::H, IE_Pressed, this, &ASLPlayerController::OnHE);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ASLPlayerController::OnReload);
	InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ASLPlayerController::OnRestart);
	InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &ASLPlayerController::TogglePanel);
	InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &ASLPlayerController::OnHelp);
	InputComponent->BindKey(EKeys::V, IE_Pressed, this, &ASLPlayerController::OnVoxels);
	InputComponent->BindKey(EKeys::X, IE_Pressed, this, &ASLPlayerController::OnWallhack);
	InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ASLPlayerController::OnSmokeHere);
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ASLPlayerController::OnJump);
	InputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &ASLPlayerController::OnCrouch);
	InputComponent->BindKey(EKeys::LeftControl, IE_Released, this, &ASLPlayerController::OnUnCrouch);
	InputComponent->BindAxisKey(EKeys::MouseX, this, &ASLPlayerController::OnTurn);
	InputComponent->BindAxisKey(EKeys::MouseY, this, &ASLPlayerController::OnLookUp);
}

void ASLPlayerController::OnMoveF(float V)
{
	ASLCharacter* C = GetSoldier();
	if (!C || C->IsDead()) return;
	const float F = (IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
	if (F != 0.f) C->AddMovementInput(FRotator(0, GetControlRotation().Yaw, 0).Vector(), F);
}

void ASLPlayerController::OnMoveR(float V)
{
	ASLCharacter* C = GetSoldier();
	if (!C || C->IsDead()) return;
	const float R = (IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
	if (R != 0.f) C->AddMovementInput(FRotationMatrix(FRotator(0, GetControlRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), R);
}

void ASLPlayerController::OnTurn(float V) { if (!bPanel) AddYawInput(V * 0.5f); }
void ASLPlayerController::OnLookUp(float V) { if (!bPanel) AddPitchInput(-V * 0.5f); }
void ASLPlayerController::OnJump() { if (ASLCharacter* C = GetSoldier()) if (!C->IsDead()) C->Jump(); }
void ASLPlayerController::OnCrouch() { if (ASLCharacter* C = GetSoldier()) C->Crouch(); }
void ASLPlayerController::OnUnCrouch() { if (ASLCharacter* C = GetSoldier()) C->UnCrouch(); }
void ASLPlayerController::OnFirePressed() { if (!bPanel) if (ASLCharacter* C = GetSoldier()) C->SetFiring(true); }
void ASLPlayerController::OnFireReleased() { if (ASLCharacter* C = GetSoldier()) C->SetFiring(false); }
void ASLPlayerController::OnUsePressed() { if (ASLCharacter* C = GetSoldier()) C->Use(true); }
void ASLPlayerController::OnUseReleased() { if (ASLCharacter* C = GetSoldier()) C->Use(false); }
void ASLPlayerController::OnSmoke() { if (ASLCharacter* C = GetSoldier()) C->Throw(ESLGrenade::Smoke); }
void ASLPlayerController::OnHE() { if (ASLCharacter* C = GetSoldier()) C->Throw(ESLGrenade::HE); }
void ASLPlayerController::OnReload() { if (ASLCharacter* C = GetSoldier()) C->Reload(); }
void ASLPlayerController::OnRestart() { Lab(ESLLab::Restart); }
void ASLPlayerController::OnVoxels() { Lab(ESLLab::ShowVoxels); }
void ASLPlayerController::OnWallhack() { Lab(ESLLab::Wallhack); }
void ASLPlayerController::OnSmokeHere() { Lab(ESLLab::SmokeAtCrosshair); }

void ASLPlayerController::TogglePanel()
{
	bPanel = !bPanel;
	bShowMouseCursor = bPanel;
	if (bPanel)
	{
		FInputModeGameAndUI M;
		M.SetHideCursorDuringCapture(false);
		M.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		SetInputMode(M);
		if (ASLCharacter* C = GetSoldier()) C->SetFiring(false);
	}
	else SetInputMode(FInputModeGameOnly());
}

// ---------------------------------------------------------------- principle panel

namespace
{
	struct FLabInfo { const TCHAR* Name; const TCHAR* Hint; bool bServer; const TCHAR* Key; };
	const FLabInfo& Info(ESLLab W)
	{
		static const FLabInfo Table[(int32)ESLLab::Count] = {
			{ TEXT("A 椭球裁剪"), TEXT("椭球内的空气体素全部填满，不管是否连通：烟会穿过薄墙"), true, TEXT("") },
			{ TEXT("B 测地填充"), TEXT("从起爆点沿相邻空气体素逐步扩散，再裁剪到椭球内：不穿墙，但被墙挡住的体积就没了"), true, TEXT("") },
			{ TEXT("C 体积守恒"), TEXT("按距离由近及远填充，直到用完开阔场地的体积：被挡住的烟沿走廊延伸（推荐）"), true, TEXT("") },
			{ TEXT("慢放生长"), TEXT("把 1.6 秒的生长放慢 4 倍，看清扩散顺序"), true, TEXT("") },
			{ TEXT("显示烟雾体素"), TEXT("用小方块画出每颗烟已经到达的体素，颜色表示到达的先后"), false, TEXT("V") },
			{ TEXT("显示关卡体素"), TEXT("画出烟雾网格范围内关卡的实心体素；铁栅为绿色（烟可穿过）"), false, TEXT("") },
			{ TEXT("子弹通道"), TEXT("子弹在烟里打出约 1 米宽、1.6 秒内收拢的通道"), true, TEXT("") },
			{ TEXT("爆炸清空"), TEXT("高爆手雷把约 5 米内的烟清到 2%"), true, TEXT("") },
			{ TEXT("回填"), TEXT("爆炸后 0.5 到 5 秒按曲线回填；关掉则清空区保持"), true, TEXT("") },
			{ TEXT("全分辨率"), TEXT("每个像素都步进：最贵"), false, TEXT("") },
			{ TEXT("1/2 分辨率"), TEXT("步进像素减为 1/4"), false, TEXT("") },
			{ TEXT("1/4 分辨率"), TEXT("步进像素减为 1/16（CS2 帧捕获中见过的设置）"), false, TEXT("") },
			{ TEXT("无噪声"), TEXT("只有体素密度：边缘呈块状"), false, TEXT("") },
			{ TEXT("程序化噪声"), TEXT("每个采样点现算两层 Worley 噪声：贵"), false, TEXT("") },
			{ TEXT("噪声纹理"), TEXT("预先烘焙的 64³ 噪声纹理：便宜（CS2 默认）"), false, TEXT("") },
			{ TEXT("光照缓存"), TEXT("每个体素先算一次光照，步进时只读取"), false, TEXT("") },
			{ TEXT("逐采样阴影射线"), TEXT("每个采样点再朝太阳走 6 步：贵"), false, TEXT("") },
			{ TEXT("不打光"), TEXT("常量颜色，只看形状"), false, TEXT("") },
			{ TEXT("双线性上采样"), TEXT("直接放大：墙角会出现漏光和锯齿"), false, TEXT("") },
			{ TEXT("深度感知上采样"), TEXT("按深度选择相近的低分辨率样本"), false, TEXT("") },
			{ TEXT("＋全分辨率补洞"), TEXT("深度都对不上的像素在全分辨率下重新步进（CS2 的 fullres pass）"), false, TEXT("") },
			{ TEXT("烟雾"), TEXT("正常显示"), false, TEXT("") },
			{ TEXT("到达距离热力图"), TEXT("按扩散到达的先后着色"), false, TEXT("") },
			{ TEXT("光照缓存视图"), TEXT("只显示每个体素缓存的入射光"), false, TEXT("") },
			{ TEXT("玩法密度下限"), TEXT("噪声只削弱边缘带，烟芯永远不透：保证各显卡看到的遮挡一致"), false, TEXT("") },
			{ TEXT("绘制烟雾"), TEXT("关掉后烟雾不再渲染，但玩法（哨兵视线）照常：对比画面与数据"), false, TEXT("") },
			{ TEXT("服务器下发结果"), TEXT("服务器填充并发送压缩的体素结果（CS2 在 2024 年 10 月后的做法）"), true, TEXT("") },
			{ TEXT("只发输入，各自填充"), TEXT("只发送起爆点与规则，每台机器用自己的关卡数据填充"), true, TEXT("") },
			{ TEXT("本机关卡数据差异"), TEXT("让这台客户端把铁栅当成实心：只发输入时两端的烟会不一样"), false, TEXT("") },
			{ TEXT("哨兵开火"), TEXT("关掉后哨兵只观察不射击，方便练习"), true, TEXT("") },
			{ TEXT("显示哨兵视线"), TEXT("画出每名哨兵到你的视线，标出穿过的烟雾量（超过 250 cm 即看不见）"), false, TEXT("") },
			{ TEXT("烟雾相关性剔除"), TEXT("服务器不再把烟后的哨兵位置发给看不见他们的客户端"), true, TEXT("") },
			{ TEXT("透视视角"), TEXT("像外挂一样，透过墙和烟显示本机已收到的敌人"), false, TEXT("X") },
			{ TEXT("重开回合"), TEXT("复活哨兵，清掉烟雾，回到出生点"), true, TEXT("Enter") },
			{ TEXT("在准星处放一颗烟"), TEXT("练习用：直接在准星指向的地面起爆一颗烟"), true, TEXT("B") },
		};
		return Table[(int32)W];
	}
}

FText ASLPlayerController::LabName(ESLLab W) { return FText::FromString(Info(W).Name); }
FText ASLPlayerController::LabHint(ESLLab W) { return FText::FromString(Info(W).Hint); }
bool ASLPlayerController::LabIsServer(ESLLab W) { return Info(W).bServer; }
FString ASLPlayerController::LabKey(ESLLab W) { return Info(W).Key; }

bool ASLPlayerController::LabOn(ESLLab W) const
{
	const ASLSmokeManager* M = GetManager();
	if (!M) return false;
	const FSLSharedSettings& S = M->Settings;
	const FSLRenderSettings& R = M->View.Render;
	switch (W)
	{
	case ESLLab::RuleA: return S.Rule == ESLSpreadRule::Ellipsoid;
	case ESLLab::RuleB: return S.Rule == ESLSpreadRule::Geodesic;
	case ESLLab::RuleC: return S.Rule == ESLSpreadRule::Volume;
	case ESLLab::SlowGrowth: return S.bSlowGrowth;
	case ESLLab::ShowVoxels: return M->View.bShowVoxels;
	case ESLLab::ShowLevelVoxels: return M->View.bShowLevelVoxels;
	case ESLLab::Bullets: return S.bBullets;
	case ESLLab::Blasts: return S.bBlasts;
	case ESLLab::Refill: return S.bRefill;
	case ESLLab::Res1: return R.ResDiv == 1;
	case ESLLab::Res2: return R.ResDiv == 2;
	case ESLLab::Res4: return R.ResDiv == 4;
	case ESLLab::NoiseNone: return R.Noise == 0;
	case ESLLab::NoiseProc: return R.Noise == 1;
	case ESLLab::NoiseTex: return R.Noise == 2;
	case ESLLab::LightCache: return R.Lighting == 0;
	case ESLLab::LightRay: return R.Lighting == 1;
	case ESLLab::LightNone: return R.Lighting == 2;
	case ESLLab::UpBilinear: return R.Upsample == 0;
	case ESLLab::UpDepth: return R.Upsample == 1;
	case ESLLab::UpFix: return R.Upsample == 2;
	case ESLLab::ViewSmoke: return R.DebugView == 0;
	case ESLLab::ViewHeat: return R.DebugView == 1;
	case ESLLab::ViewCache: return R.DebugView == 2;
	case ESLLab::GameplayFloor: return R.bGameplayFloor;
	case ESLLab::SmokeOff: return R.bEnabled;
	case ESLLab::SyncServer: return S.Sync == ESLSyncMode::ServerResult;
	case ESLLab::SyncLocal: return S.Sync == ESLSyncMode::LocalFromInputs;
	case ESLLab::InjectDiff: return M->View.bInjectDiff;
	case ESLLab::Cull: return S.bRelevancyCull;
	case ESLLab::Wallhack: return M->View.bWallhack;
	case ESLLab::Sight: return M->View.bShowSight;
	case ESLLab::BotsFire: return S.bBotsFire;
	default: return false;
	}
}

bool ASLPlayerController::LabEnabled(ESLLab W) const
{
	const ASLSmokeManager* M = GetManager();
	if (!M) return false;
	const FSLRenderSettings& R = M->View.Render;
	switch (W)
	{
	case ESLLab::Refill: return M->Settings.bBlasts;
	case ESLLab::Res1: case ESLLab::Res2: case ESLLab::Res4:
	case ESLLab::NoiseNone: case ESLLab::NoiseProc: case ESLLab::NoiseTex:
	case ESLLab::LightCache: case ESLLab::LightRay: case ESLLab::LightNone:
	case ESLLab::ViewSmoke: case ESLLab::ViewHeat: case ESLLab::ViewCache: return R.bEnabled;
	case ESLLab::UpBilinear: case ESLLab::UpDepth: case ESLLab::UpFix: return R.bEnabled && R.ResDiv > 1;
	case ESLLab::GameplayFloor: return R.bEnabled && R.Noise != 0;
	case ESLLab::InjectDiff: return M->Settings.Sync == ESLSyncMode::LocalFromInputs && !HasAuthority();
	default: return true;
	}
}

void ASLPlayerController::ApplyShared(TFunctionRef<void(FSLSharedSettings&)> Fn)
{
	ASLSmokeManager* M = GetManager();
	if (!M) return;
	FSLSharedSettings S = M->Settings;
	Fn(S);
	if (HasAuthority()) M->Settings = S;
	else { M->Settings = S; ServerSetShared(S); }	// show it at once; the server's copy wins
}

void ASLPlayerController::ServerSetShared_Implementation(const FSLSharedSettings& S)
{
	if (ASLSmokeManager* M = GetManager()) M->Settings = S;
}

void ASLPlayerController::ServerRestart_Implementation()
{
	if (ASLGameMode* GM = GetWorld()->GetAuthGameMode<ASLGameMode>()) GM->ResetRound();
}

void ASLPlayerController::ServerSmokeAt_Implementation(FVector_NetQuantize Where)
{
	if (ASLSmokeManager* M = GetManager()) M->SpawnSmoke(Where);
}

void ASLPlayerController::ServerTeleport_Implementation(FVector_NetQuantize Where)
{
	if (ASLCharacter* C = GetSoldier())
	{
		C->TeleportTo(Where, C->GetActorRotation(), false, true);
		C->GetCharacterMovement()->StopMovementImmediately();
	}
}

void ASLPlayerController::Lab(ESLLab W)
{
	ASLSmokeManager* M = GetManager();
	if (!M || !LabEnabled(W)) return;
	FSLRenderSettings& R = M->View.Render;
	switch (W)
	{
	case ESLLab::RuleA: ApplyShared([](FSLSharedSettings& S) { S.Rule = ESLSpreadRule::Ellipsoid; }); break;
	case ESLLab::RuleB: ApplyShared([](FSLSharedSettings& S) { S.Rule = ESLSpreadRule::Geodesic; }); break;
	case ESLLab::RuleC: ApplyShared([](FSLSharedSettings& S) { S.Rule = ESLSpreadRule::Volume; }); break;
	case ESLLab::SlowGrowth: ApplyShared([](FSLSharedSettings& S) { S.bSlowGrowth = !S.bSlowGrowth; }); break;
	case ESLLab::ShowVoxels: M->View.bShowVoxels = !M->View.bShowVoxels; break;
	case ESLLab::ShowLevelVoxels: M->View.bShowLevelVoxels = !M->View.bShowLevelVoxels; break;
	case ESLLab::Bullets: ApplyShared([](FSLSharedSettings& S) { S.bBullets = !S.bBullets; }); break;
	case ESLLab::Blasts: ApplyShared([](FSLSharedSettings& S) { S.bBlasts = !S.bBlasts; }); break;
	case ESLLab::Refill: ApplyShared([](FSLSharedSettings& S) { S.bRefill = !S.bRefill; }); break;
	case ESLLab::Res1: R.ResDiv = 1; break;
	case ESLLab::Res2: R.ResDiv = 2; break;
	case ESLLab::Res4: R.ResDiv = 4; break;
	case ESLLab::NoiseNone: R.Noise = 0; break;
	case ESLLab::NoiseProc: R.Noise = 1; break;
	case ESLLab::NoiseTex: R.Noise = 2; break;
	case ESLLab::LightCache: R.Lighting = 0; break;
	case ESLLab::LightRay: R.Lighting = 1; break;
	case ESLLab::LightNone: R.Lighting = 2; break;
	case ESLLab::UpBilinear: R.Upsample = 0; break;
	case ESLLab::UpDepth: R.Upsample = 1; break;
	case ESLLab::UpFix: R.Upsample = 2; break;
	case ESLLab::ViewSmoke: R.DebugView = 0; break;
	case ESLLab::ViewHeat: R.DebugView = 1; break;
	case ESLLab::ViewCache: R.DebugView = 2; break;
	case ESLLab::GameplayFloor: R.bGameplayFloor = !R.bGameplayFloor; break;
	case ESLLab::SmokeOff: R.bEnabled = !R.bEnabled; break;
	case ESLLab::SyncServer: ApplyShared([](FSLSharedSettings& S) { S.Sync = ESLSyncMode::ServerResult; }); break;
	case ESLLab::SyncLocal: ApplyShared([](FSLSharedSettings& S) { S.Sync = ESLSyncMode::LocalFromInputs; }); break;
	case ESLLab::InjectDiff: M->SetInjectDiff(!M->View.bInjectDiff); break;
	case ESLLab::Cull: ApplyShared([](FSLSharedSettings& S) { S.bRelevancyCull = !S.bRelevancyCull; }); break;
	case ESLLab::Wallhack: M->View.bWallhack = !M->View.bWallhack; break;
	case ESLLab::Sight: M->View.bShowSight = !M->View.bShowSight; break;
	case ESLLab::BotsFire: ApplyShared([](FSLSharedSettings& S) { S.bBotsFire = !S.bBotsFire; }); break;
	case ESLLab::Restart: ServerRestart(); break;
	case ESLLab::SmokeAtCrosshair:
	{
		FVector Loc; FRotator Rot;
		GetPlayerViewPoint(Loc, Rot);
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(SLAim), false, GetPawn());
		if (GetWorld()->LineTraceSingleByChannel(Hit, Loc, Loc + Rot.Vector() * 6000.f, ECC_Visibility, Q))
			ServerSmokeAt(Hit.ImpactPoint + Hit.ImpactNormal * 20.f);
		break;
	}
	default: break;
	}
}

// ---------------------------------------------------------------- per frame

void ASLPlayerController::PlayerTick(float Dt)
{
	Super::PlayerTick(Dt);
	if (!IsLocalController()) return;
	FrameMs = FMath::Lerp(FrameMs, Dt * 1000.f, 0.05f);
	if (ASLSmokeManager* M = GetManager()) M->LocalPlayerId = PlayerState ? PlayerState->GetPlayerId() : -1;
	OnMoveF(0.f);
	OnMoveR(0.f);
	DrawWorldOverlays();
	if (!CaptureDir.IsEmpty()) TickCapture(Dt);
}

void ASLPlayerController::DrawWorldOverlays()
{
	ASLSmokeManager* M = GetManager();
	if (!M || !Hud) return;
	Hud->Labels.Reset();
	ASLCharacter* Me = GetSoldier();
	auto Label = [&](const FVector& At, const FString& S, FLinearColor C)
	{
		FVector2D P;
		if (ProjectWorldLocationToScreen(At, P, true))
		{
			const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
			Hud->Labels.Add({ P / FMath::Max(Scale, 0.01f), S, C });
		}
	};
	FVector Eye; FRotator Rot;
	GetPlayerViewPoint(Eye, Rot);
	int32 Known = 0;
	for (TActorIterator<ASLBot> It(GetWorld()); It; ++It)
	{
		ASLBot* B = *It;
		if (B->IsDead()) continue;
		++Known;
		if (M->View.bWallhack)
		{
			// what a cheat could draw: every enemy whose position this machine has received
			const bool bWouldCull = HasAuthority() && M->Settings.bRelevancyCull && B->SmokeHides(Eye);
			const FColor C = bWouldCull ? FColor(140, 140, 140) : FColor(255, 70, 50);
			DrawDebugCapsule(GetWorld(), B->GetActorLocation(), 92.f, 38.f, FQuat::Identity, C, false, -1.f, SDPG_Foreground, 2.f);
			Label(B->GetActorLocation() + FVector(0, 0, 120), bWouldCull ? FString::Printf(TEXT("哨兵 %d · 烟后：服务器不会发给远端玩家"), B->Index + 1)
				: FString::Printf(TEXT("哨兵 %d · 本机已收到位置"), B->Index + 1), bWouldCull ? FLinearColor(0.7f, 0.7f, 0.7f) : FLinearColor(1.f, 0.4f, 0.3f));
		}
		if (M->View.bShowSight && Me && !Me->IsDead())
		{
			const FVector A = B->EyeLocation(), T = Me->ChestLocation();
			FHitResult Hit;
			FCollisionQueryParams Q(SCENE_QUERY_STAT(SLSight), false, B);
			Q.AddIgnoredActor(Me);
			const bool bClear = !GetWorld()->LineTraceSingleByChannel(Hit, A, T, ECC_Visibility, Q);
			if (!bClear) continue;
			const float Cm = M->SmokeLength(A, T);
			const bool bBlocked = Cm >= SL::BotSmokeBlockCm * 0.5f;
			// Stop the drawn line 4 m short of me, otherwise it runs straight down the first-person camera.
			const FVector Tip = T - (T - A).GetSafeNormal() * FMath::Min(400.f, FVector::Dist(A, T));
			DrawDebugLine(GetWorld(), A, Tip, bBlocked ? FColor(80, 220, 120) : FColor(255, 60, 40), false, -1.f, SDPG_World, 1.f);
			Label(FMath::Lerp(A, T, 0.35f), FString::Printf(TEXT("哨兵 %d：烟 %.0f cm %s"), B->Index + 1, Cm, bBlocked ? TEXT("· 看不见") : TEXT("· 看得见")),
				bBlocked ? FLinearColor(0.4f, 0.95f, 0.5f) : FLinearColor(1.f, 0.4f, 0.3f));
		}
	}
	Metrics.Add(TEXT("known_bots"), Known);
}
