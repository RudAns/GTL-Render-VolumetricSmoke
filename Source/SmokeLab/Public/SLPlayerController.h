#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SLTypes.h"
#include "SLPlayerController.generated.h"

/** Every switch of the principle panel. Scope: S = server (affects everyone), L = this machine only. */
UENUM()
enum class ESLLab : uint8
{
	// spreading (S, radio) + local views
	RuleA, RuleB, RuleC, SlowGrowth, ShowVoxels, ShowLevelVoxels,
	// interaction (S, layered)
	Bullets, Blasts, Refill,
	// rendering (L)
	Res1, Res2, Res4, NoiseNone, NoiseProc, NoiseTex, LightCache, LightRay, LightNone,
	UpBilinear, UpDepth, UpFix, ViewSmoke, ViewHeat, ViewCache, GameplayFloor, SmokeOff,
	// network (S radio + L)
	SyncServer, SyncLocal, InjectDiff,
	// fairness
	Cull, Wallhack, Sight, BotsFire,
	// actions
	Restart, SmokeAtCrosshair,
	Count
};

UCLASS()
class SMOKELAB_API ASLPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ASLPlayerController();
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float Dt) override;
	virtual void OnPossess(APawn* P) override;

	void Lab(ESLLab Which);
	bool LabOn(ESLLab Which) const;
	bool LabEnabled(ESLLab Which) const;	// greyed out when its parent is off
	static FText LabName(ESLLab Which);
	static FText LabHint(ESLLab Which);
	static bool LabIsServer(ESLLab Which);
	static FString LabKey(ESLLab Which);

	class ASLCharacter* GetSoldier() const;
	class ASLSmokeManager* GetManager() const;
	bool IsPanelOpen() const { return bPanel; }
	void TogglePanel();
	bool IsHelpOpen() const { return bHelp; }

	UFUNCTION(Server, Reliable) void ServerSetShared(const FSLSharedSettings& S);
	UFUNCTION(Server, Reliable) void ServerRestart();
	UFUNCTION(Server, Reliable) void ServerSmokeAt(FVector_NetQuantize Where);
	UFUNCTION(Server, Reliable) void ServerTeleport(FVector_NetQuantize Where);	// self-test only: a client cannot move its own pawn far

	FString WindowSide;			// -SLSide=L|R: left or right half of the primary monitor (Run2P)
	FString JoinAddress;		// -SLJoin=<address>: keep retrying until connected
	FTimerHandle JoinTimer;
	void PlaceWindow();
	void RetryJoin();

	// --- self-test (-SLCapture=<dir> -SLCaptureRole=host|server|client) ---
	FString CaptureDir, CaptureRole;
	float CaptureT = 0.f;
	int32 CaptureStep = 0;
	TMap<FString, float> Metrics;
	float FrameMs = 16.f;
	void Shot(const FString& Name);
	void Press(const FKey& K);
	void Hold(const FKey& K, bool bDown);
	void Check(bool bOk, const FString& Name, const FString& Detail);
	void Look(float Yaw, float Pitch);
	void TickCapture(float Dt);
	void Teleport(const FVector& Where, float Yaw, float Pitch = 0.f);

private:
	UPROPERTY() TObjectPtr<class USLHud> Hud;
	bool bPanel = false;
	bool bHelp = true;
	FSLSharedSettings PendingShared;
	void ApplyShared(TFunctionRef<void(FSLSharedSettings&)> Fn);
	void DrawWorldOverlays();
	// input
	void OnFirePressed(); void OnFireReleased();
	void OnUsePressed(); void OnUseReleased();
	void OnSmoke(); void OnHE(); void OnReload(); void OnRestart();
	void OnMoveF(float V); void OnMoveR(float V); void OnTurn(float V); void OnLookUp(float V);
	void OnJump(); void OnCrouch(); void OnUnCrouch();
	void OnHelp() { bHelp = !bHelp; }
	void OnVoxels(); void OnWallhack(); void OnSmokeHere();
};
