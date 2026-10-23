// Scripted self-test (-SLCapture=<dir> -SLCaptureRole=host|server|client).
// Writes PNG shots and <role>_results.txt with PASS/FAIL lines, then quits.

#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "SLBot.h"
#include "SLCharacter.h"
#include "SLGame.h"
#include "SLGrenade.h"
#include "SLLevel.h"
#include "SLPlayerController.h"
#include "SLSmoke.h"
#include "SLSmokeManager.h"
#include "UnrealClient.h"

namespace
{
	struct FStep { float At; TFunction<void(ASLPlayerController*)> Do; };
}

void ASLPlayerController::Shot(const FString& Name)
{
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(CaptureDir, Name + TEXT(".png")), true, false);
}

void ASLPlayerController::Press(const FKey& K)
{
	InputKey(FInputKeyParams(K, IE_Pressed, 1.0, false));
	InputKey(FInputKeyParams(K, IE_Released, 0.0, false));
}

void ASLPlayerController::Hold(const FKey& K, bool bDown)
{
	InputKey(FInputKeyParams(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.0 : 0.0, false));
}

void ASLPlayerController::Check(bool bOk, const FString& Name, const FString& Detail)
{
	const FString Line = FString::Printf(TEXT("%s %s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
	UE_LOG(LogTemp, Display, TEXT("SLTEST %s"), *Line);
	FFileHelper::SaveStringToFile(Line + TEXT("\n"), *FPaths::Combine(CaptureDir, CaptureRole + TEXT("_results.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

void ASLPlayerController::Look(float Yaw, float Pitch)
{
	SetControlRotation(FRotator(Pitch, Yaw, 0));
}

void ASLPlayerController::Teleport(const FVector& Where, float Yaw, float Pitch)
{
	if (ASLCharacter* C = GetSoldier())
	{
		C->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
		C->GetCharacterMovement()->StopMovementImmediately();
	}
	Look(Yaw, Pitch);
}

void ASLPlayerController::TickCapture(float Dt)
{
	static TArray<FStep> Script;
	static bool bBuilt = false;
	CaptureT += Dt;
	if (!bBuilt)
	{
		bBuilt = true;
		auto M = [](ASLPlayerController* P) { return P->GetManager(); };
		auto Spots = [](ASLPlayerController* P) -> const FSLLevelSpots& { static FSLLevelSpots None; ASLLevel* L = ASLLevel::Get(P); return L ? L->Spots() : None; };
		auto SmokeCount = [](ASLPlayerController* P) { int32 N = 0; if (ASLSmokeManager* Mg = P->GetManager()) for (auto& S : Mg->GetSmokes()) if (S.IsValid() && S->HasField()) ++N; return N; };
		auto Restart = [](ASLPlayerController* P) { P->Lab(ESLLab::Restart); };
		auto Seen = [](ASLPlayerController* P) { ASLCharacter* C = P->GetSoldier(); return C && C->bSeenByBot; };
		auto Bot = [](ASLPlayerController* P, int32 I) -> ASLBot* { for (TActorIterator<ASLBot> It(P->GetWorld()); It; ++It) if (It->Index == I) return *It; return nullptr; };
		if (CaptureRole == TEXT("host"))
		{
			Script = {
				{ 2.0f, [](ASLPlayerController* P) { if (P->LabOn(ESLLab::BotsFire)) P->Lab(ESLLab::BotsFire); P->Teleport(FVector(260, 260, 100), 32.f, -6.f); } },
				{ 4.0f, [](ASLPlayerController* P) { P->Shot(TEXT("00_courtyard")); } },
				// 1. rule C: a smoke inside the corridor spreads along it
				{ 4.5f, [Spots](ASLPlayerController* P) { P->Teleport(FVector(1120, 800, 100), 0.f, -4.f); P->ServerSmokeAt(Spots(P).CorridorMouth + FVector(450, 0, 5)); } },
				{ 7.5f, [SmokeCount](ASLPlayerController* P) {
					ASLSmokeManager* Mg = P->GetManager();
					P->Check(SmokeCount(P) == 1, TEXT("smoke_spawned"), FString::Printf(TEXT("filled %d voxels in %.0f us"), Mg->LastFillFilled, Mg->LastFillMicros));
					P->Check(Mg->GpuTimes.MarchMs > 0.f, TEXT("gpu_timers"), FString::Printf(TEXT("light %.2f march %.2f composite %.2f ms at %dx%d"), Mg->GpuTimes.LightMs, Mg->GpuTimes.MarchMs, Mg->GpuTimes.CompositeMs, Mg->GpuTimes.LowW, Mg->GpuTimes.LowH));
					P->Shot(TEXT("01_corridor_ruleC")); } },
				// 2. bullets carve tunnels through it
				{ 8.0f, [](ASLPlayerController* P) { P->Hold(EKeys::LeftMouseButton, true); } },
				{ 8.012f, [](ASLPlayerController* P) { P->Shot(TEXT("02a_tracer")); } },
				{ 8.03f, [](ASLPlayerController* P) { P->Shot(TEXT("02b_tracer")); } },
				{ 8.04f, [](ASLPlayerController* P) { P->Hold(EKeys::LeftMouseButton, false); } },
				{ 8.35f, [](ASLPlayerController* P) { const ASLSmokeManager* Mg = P->GetManager();
					// the round leaves the first-person muzzle; compare the smoke along its path without and with carving
					// on the host the predicted copy is dropped as soon as the authoritative event exists
					const TArray<FSLBulletEvent>& L = Mg->Bullets.Num() ? Mg->Bullets : Mg->GetLocalBullets();
					const bool bShot = L.Num() > 0;
					const FSLBulletEvent B = bShot ? L.Last() : FSLBulletEvent();
					const float Before = bShot ? Mg->SmokeLength(B.Start, B.End, false) : 0.f, After = bShot ? Mg->SmokeLength(B.Start, B.End, true) : 0.f;
					const float FromEye = bShot ? FVector::Dist(B.Start, P->PlayerCameraManager->GetCameraLocation()) : 0.f;
					P->Check(bShot && Before > 300.f && After < Before * 0.3f && FromEye > 10.f && FromEye < 120.f, TEXT("bullet_tunnel"),
						FString::Printf(TEXT("round starts %.0f cm from the eye at the muzzle; smoke along its path %.0f cm uncarved, %.0f cm carved"), FromEye, Before, After));
					P->Shot(TEXT("02_bullet_tunnels")); } },
				// 3. an HE grenade clears it, then it refills
				{ 9.5f, [](ASLPlayerController* P) { P->Look(0.f, 4.f); P->Press(EKeys::H); } },
				{ 11.6f, [](ASLPlayerController* P) { const ASLSmokeManager* Mg = P->GetManager();
					P->Metrics.Add(TEXT("d_he"), Mg->DensityAt(FVector(1950, 800, 120)));
					P->Teleport(FVector(1950, 800, 100), 0.f, -6.f); } },
				{ 11.9f, [](ASLPlayerController* P) { P->Shot(TEXT("03_he_cleared"));
					const ASLSmokeManager* Mg = P->GetManager(); const FVector Cam = P->PlayerCameraManager->GetCameraLocation();
					P->Check(true, TEXT("he_debug"), FString::Printf(TEXT("blasts %d at %s mask %u, cam %s density at cam %.2f"), Mg->Blasts.Num(),
						Mg->Blasts.Num() ? *FVector(Mg->Blasts[0].Pos).ToString() : TEXT("-"), Mg->Blasts.Num() ? Mg->Blasts[0].Mask : 0u, *Cam.ToString(), Mg->DensityAt(Cam))); } },
				{ 12.2f, [](ASLPlayerController* P) { P->Teleport(FVector(1120, 800, 100), 0.f, 0.f); } },
				{ 16.5f, [](ASLPlayerController* P) { const ASLSmokeManager* Mg = P->GetManager();
					const float D = Mg->DensityAt(FVector(1950, 800, 120));
					P->Check(P->Metrics[TEXT("d_he")] < 0.2f && D > 0.5f, TEXT("he_clear_refill"), FString::Printf(TEXT("density at the blast %.2f just after, %.2f five seconds later"), P->Metrics[TEXT("d_he")], D));
					P->Shot(TEXT("04_he_refilled")); } },
				// 4. thin wall: rule A leaks through it, rule C does not
				{ 17.0f, [Restart](ASLPlayerController* P) { Restart(P); if (P->LabOn(ESLLab::BotsFire)) P->Lab(ESLLab::BotsFire); } },
				{ 17.5f, [](ASLPlayerController* P) { P->Lab(ESLLab::RuleA); P->Lab(ESLLab::ShowLevelVoxels); } },
				{ 18.0f, [Spots](ASLPlayerController* P) { P->ServerSmokeAt(Spots(P).ThinWall + FVector(-80, 0, 5)); P->Teleport(FVector(1250, 2850, 160), 205.f, -10.f); } },
				{ 21.0f, [](ASLPlayerController* P) { const ASLSmokeManager* Mg = P->GetManager();
					P->Check(Mg->LastFillLeaked > 0, TEXT("ruleA_leaks"), FString::Printf(TEXT("%d of %d voxels on the far side of the thin wall"), Mg->LastFillLeaked, Mg->LastFillFilled));
					P->Shot(TEXT("05_ruleA_leak")); } },
				{ 21.5f, [Restart](ASLPlayerController* P) { Restart(P); if (P->LabOn(ESLLab::BotsFire)) P->Lab(ESLLab::BotsFire); P->Lab(ESLLab::RuleC); } },
				{ 22.5f, [Spots](ASLPlayerController* P) { P->ServerSmokeAt(Spots(P).ThinWall + FVector(-80, 0, 5)); P->Teleport(FVector(1250, 2850, 160), 205.f, -10.f); } },
				{ 25.5f, [](ASLPlayerController* P) { const ASLSmokeManager* Mg = P->GetManager();
					P->Check(Mg->LastFillLeaked == 0, TEXT("ruleC_no_leak"), FString::Printf(TEXT("%d voxels"), Mg->LastFillFilled));
					P->Shot(TEXT("06_ruleC_wall")); } },
				{ 25.8f, [](ASLPlayerController* P) { P->Lab(ESLLab::ShowLevelVoxels); P->Lab(ESLLab::ShowVoxels); } },
				{ 26.5f, [](ASLPlayerController* P) { P->Teleport(FVector(150, 2950, 380), -47.f, -19.f); /* west of the thin wall, where the smoke is */ } },
				{ 27.5f, [](ASLPlayerController* P) { P->Shot(TEXT("07_voxels")); } },
				{ 27.8f, [](ASLPlayerController* P) { P->Lab(ESLLab::ShowVoxels); P->Lab(ESLLab::ViewHeat); } },	// after the screenshot frame
				{ 28.5f, [](ASLPlayerController* P) { P->Shot(TEXT("08_heatmap")); } },
				{ 28.8f, [](ASLPlayerController* P) { P->Lab(ESLLab::ViewSmoke); } },	// after the screenshot frame
				// 5. the loop: a sentry sees me in mid, a smoke between us blinds it
				{ 29.0f, [Restart](ASLPlayerController* P) { Restart(P); } },
				{ 30.0f, [](ASLPlayerController* P) { P->Lab(ESLLab::Sight); P->Teleport(FVector(2300, 2000, 100), 0.f, 0.f); } },
				{ 31.5f, [Seen](ASLPlayerController* P) { P->Check(Seen(P), TEXT("sentry_sees_me"), TEXT("standing in the open mid lane")); P->Shot(TEXT("09_seen_in_mid")); } },
				{ 31.6f, [](ASLPlayerController* P) { P->ServerSmokeAt(FVector(3200, 2000, 5)); } },
				{ 34.5f, [Seen, Bot](ASLPlayerController* P) {
					float Cm = 0.f; bool bClear = false; ASLBot* B = Bot(P, 2);
					if (B) B->CanSee(P->GetSoldier(), Cm, bClear);
					P->Check(!Seen(P), TEXT("smoke_blinds_sentry"), FString::Printf(TEXT("%.0f cm of smoke on the sight line"), Cm));
					P->Shot(TEXT("10_blinded_by_smoke")); } },
				{ 35.0f, [](ASLPlayerController* P) { P->Lab(ESLLab::Sight); P->Lab(ESLLab::Wallhack); P->Teleport(FVector(2300, 2000, 100), 0.f, 0.f); } },
				{ 36.0f, [](ASLPlayerController* P) { P->Shot(TEXT("11_wallhack_view")); } },
				{ 36.3f, [](ASLPlayerController* P) { P->Lab(ESLLab::Wallhack); } },	// after the screenshot frame
				// 6. door: opening it lets the corridor smoke through
				{ 36.5f, [Restart](ASLPlayerController* P) { Restart(P); if (P->LabOn(ESLLab::BotsFire)) P->Lab(ESLLab::BotsFire); } },
				{ 37.5f, [](ASLPlayerController* P) { P->ServerSmokeAt(FVector(2150, 800, 5)); P->Teleport(FVector(2330, 700, 100), 0.f, 0.f); } },
				{ 40.0f, [](ASLPlayerController* P) { P->Metrics.Add(TEXT("door_before"), P->GetManager()->DensityAt(FVector(2560, 800, 120))); P->Press(EKeys::E); } },
				{ 42.5f, [](ASLPlayerController* P) { const float D = P->GetManager()->DensityAt(FVector(2560, 800, 120));
					P->Check(D > P->Metrics[TEXT("door_before")] + 0.3f, TEXT("door_refill"), FString::Printf(TEXT("density past the door %.2f -> %.2f"), P->Metrics[TEXT("door_before")], D));
					P->Teleport(FVector(3100, 750, 100), 180.f, 0.f); } },
				{ 43.5f, [](ASLPlayerController* P) { P->Shot(TEXT("12_door_open")); } },
				// 7. plant
				{ 44.0f, [Restart](ASLPlayerController* P) { Restart(P); if (P->LabOn(ESLLab::BotsFire)) P->Lab(ESLLab::BotsFire); } },
				{ 45.0f, [](ASLPlayerController* P) { P->Teleport(FVector(4200, 600, 100), 0.f, -10.f); } },
				{ 45.5f, [](ASLPlayerController* P) { P->Hold(EKeys::E, true); } },
				{ 49.2f, [](ASLPlayerController* P) { P->Hold(EKeys::E, false); const ASLGameState* GS = ASLGameState::Get(P);
					P->Check(GS && GS->Round == ESLRound::Planted, TEXT("plant"), TEXT("held E for 3.7 s in the zone")); P->Shot(TEXT("13_planted")); } },
				// 8. panel and render modes
				{ 50.0f, [Restart](ASLPlayerController* P) { Restart(P); if (P->LabOn(ESLLab::BotsFire)) P->Lab(ESLLab::BotsFire); } },
				{ 51.0f, [](ASLPlayerController* P) { P->ServerSmokeAt(FVector(4000, 1000, 5)); P->Teleport(FVector(3300, 800, 100), 15.f, 0.f); } },
				{ 54.0f, [](ASLPlayerController* P) { P->Shot(TEXT("14_site_smoke")); } },
				{ 54.3f, [](ASLPlayerController* P) { P->TogglePanel(); } },	// after the screenshot frame
				{ 55.0f, [](ASLPlayerController* P) { P->Shot(TEXT("15_panel")); } },
				{ 55.3f, [](ASLPlayerController* P) { P->TogglePanel(); P->Lab(ESLLab::Res1); P->Lab(ESLLab::NoiseProc); } },	// after the screenshot frame
				{ 57.0f, [](ASLPlayerController* P) { const ASLSmokeManager* Mg = P->GetManager(); P->Metrics.Add(TEXT("full_proc"), Mg->GpuTimes.MarchMs);
					P->Lab(ESLLab::Res4); P->Lab(ESLLab::NoiseTex); } },
				{ 59.0f, [](ASLPlayerController* P) { const ASLSmokeManager* Mg = P->GetManager();
					P->Check(Mg->GpuTimes.MarchMs < P->Metrics[TEXT("full_proc")], TEXT("quarter_cheaper"), FString::Printf(TEXT("march %.2f ms full-res procedural, %.2f ms quarter-res texture"), P->Metrics[TEXT("full_proc")], Mg->GpuTimes.MarchMs)); } },
				{ 60.0f, [](ASLPlayerController* P) { P->Check(P->FrameMs < 40.f, TEXT("frame_time"), FString::Printf(TEXT("%.1f ms/frame"), P->FrameMs)); P->ConsoleCommand(TEXT("quit")); } },
			};
		}
		else if (CaptureRole == TEXT("client"))
		{
			// Runs on a client joined to a listen server over 127.0.0.1. Everything shared goes through server RPCs.
			auto Mismatch = [](ASLPlayerController* P) { int32 N = 0; for (auto& S : P->GetManager()->GetSmokes()) if (S.IsValid() && S->HasField() && S->FieldCrc != S->ServerFieldCrc) ++N; return N; };
			auto BotCount = [](ASLPlayerController* P) { int32 N = 0; for (TActorIterator<ASLBot> It(P->GetWorld()); It; ++It) ++N; return N; };
			Script = {
				{ 2.0f, [](ASLPlayerController* P) { if (P->LabOn(ESLLab::BotsFire)) P->Lab(ESLLab::BotsFire); P->ServerTeleport(FVector(1120, 800, 100)); P->Look(0.f, -4.f); } },
				// 1. server result: the client receives the packed field and its CRC matches the server's
				{ 3.0f, [Spots](ASLPlayerController* P) { P->ServerSmokeAt(Spots(P).CorridorMouth + FVector(450, 0, 5)); } },
				{ 7.0f, [SmokeCount, Mismatch](ASLPlayerController* P) { ASLSmokeManager* Mg = P->GetManager(); const ASLSmoke* S = Mg->GetSmokes().Num() ? Mg->GetSmokes()[0].Get() : nullptr;
					P->Check(SmokeCount(P) == 1 && S && S->bFieldFromServer && Mismatch(P) == 0, TEXT("client_server_result"),
						FString::Printf(TEXT("%d voxels arrived in %d bytes, crc %08x = server %08x"), S ? S->Field.Filled : 0, S ? S->ReplicatedBytes : 0, S ? S->FieldCrc : 0u, S ? S->ServerFieldCrc : 0u));
					P->Shot(TEXT("c00_client_corridor")); } },
				// 2. inputs only + an injected difference: this client thinks the grate is solid, the server lets smoke through it
				{ 7.5f, [Restart](ASLPlayerController* P) { P->Lab(ESLLab::SyncLocal); Restart(P); } },
				{ 8.5f, [](ASLPlayerController* P) { if (!P->LabOn(ESLLab::InjectDiff)) P->Lab(ESLLab::InjectDiff); P->ServerTeleport(FVector(2900, 1450, 100)); P->Look(-90.f, -8.f); } },
				{ 9.0f, [](ASLPlayerController* P) { P->ServerSmokeAt(FVector(2900, 850, 5)); } },
				{ 13.0f, [Mismatch](ASLPlayerController* P) { const ASLSmokeManager* Mg = P->GetManager(); const ASLSmoke* S = Mg->GetSmokes().Num() ? Mg->GetSmokes()[0].Get() : nullptr;
					P->Check(Mismatch(P) >= 1 && S && !S->bFieldFromServer, TEXT("desync_detected"),
						FString::Printf(TEXT("local fill %d voxels, crc %08x vs server %08x, %d bytes on the wire"), S ? S->Field.Filled : 0, S ? S->FieldCrc : 0u, S ? S->ServerFieldCrc : 0u, S ? S->ReplicatedBytes : 0));
					P->Shot(TEXT("c01_desync")); } },
				{ 13.3f, [](ASLPlayerController* P) { P->Lab(ESLLab::ShowLevelVoxels); } },
				{ 14.3f, [](ASLPlayerController* P) { P->Shot(TEXT("c02_desync_voxels")); } },
				{ 14.6f, [Restart](ASLPlayerController* P) { P->Lab(ESLLab::ShowLevelVoxels); P->Lab(ESLLab::InjectDiff); P->Lab(ESLLab::SyncServer); Restart(P); } },
				// 3. relevancy culling: a sentry fully behind smoke is no longer sent to this client
				{ 16.0f, [](ASLPlayerController* P) { if (!P->LabOn(ESLLab::Cull)) P->Lab(ESLLab::Cull); if (!P->LabOn(ESLLab::Wallhack)) P->Lab(ESLLab::Wallhack);
					P->ServerTeleport(FVector(2300, 2000, 100)); P->Look(0.f, 0.f); } },
				{ 18.0f, [BotCount](ASLPlayerController* P) { P->Metrics.Add(TEXT("bots_before"), BotCount(P)); P->Shot(TEXT("c03_cull_before")); } },
				{ 18.3f, [](ASLPlayerController* P) { P->ServerSmokeAt(FVector(3200, 2000, 5)); } },
				{ 24.0f, [BotCount](ASLPlayerController* P) { const int32 N = BotCount(P);
					P->Check(N < P->Metrics[TEXT("bots_before")], TEXT("relevancy_cull"), FString::Printf(TEXT("sentries known to this client: %.0f before the smoke, %d after"), P->Metrics[TEXT("bots_before")], N));
					P->Shot(TEXT("c04_cull_after")); } },
				{ 24.3f, [](ASLPlayerController* P) { P->Lab(ESLLab::Cull); } },
				{ 26.5f, [BotCount](ASLPlayerController* P) { P->Check(BotCount(P) == (int32)P->Metrics[TEXT("bots_before")], TEXT("cull_off_restores"), FString::Printf(TEXT("%d sentries"), BotCount(P)));
					P->Lab(ESLLab::Wallhack); } },
				{ 27.0f, [](ASLPlayerController* P) { P->Check(P->FrameMs < 40.f, TEXT("client_frame_time"), FString::Printf(TEXT("%.1f ms/frame"), P->FrameMs)); P->ConsoleCommand(TEXT("quit")); } },
			};
		}
		else
		{
			Script = { { 90.f, [](ASLPlayerController* P) { P->ConsoleCommand(TEXT("quit")); } } };
		}
	}
	while (CaptureStep < Script.Num() && CaptureT >= Script[CaptureStep].At)
	{
		Script[CaptureStep].Do(this);
		++CaptureStep;
	}
}
