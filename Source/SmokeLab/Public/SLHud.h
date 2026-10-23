#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SLPlayerController.h"
#include "SLHud.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UCanvasPanelSlot;
class UTextBlock;
class UVerticalBox;

UCLASS()
class USLClick : public UObject
{
	GENERATED_BODY()
public:
	ESLLab Which = ESLLab::Count;
	TWeakObjectPtr<ASLPlayerController> Owner;
	UFUNCTION() void OnClicked();
};

/** Game HUD + the principle panel, built in code (UMG). */
UCLASS()
class USLHud : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& Geo, float Dt) override;
	/** World-space labels drawn by the controller (projected to screen). */
	struct FLabel { FVector2D Pos; FString Text; FLinearColor Color; };
	TArray<FLabel> Labels;

private:
	ASLPlayerController* PC() const;
	UTextBlock* Text(int32 Size, FLinearColor Color, bool bBold = false, float Wrap = 0.f);
	UBorder* Panel(FLinearColor Fill, FLinearColor Rim, float Radius, FMargin Pad);
	UBorder* KeyCap(const FString& Key, int32 Size = 12);
	UCanvasPanelSlot* Put(UWidget* W, FVector2D Anchor, FVector2D Align, FVector2D Pos);
	void Group(UVerticalBox* Box, const FString& Title, const FString& Sub);
	void Row(UVerticalBox* Box, ESLLab Which, bool bRadio, int32 Indent);
	void SubHead(UVerticalBox* Box, const FString& Title, int32 Indent);
	UButton* Click(ESLLab Which, UWidget* Content);

	UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY() TArray<TObjectPtr<USLClick>> Clicks;
	// panel
	UPROPERTY() TObjectPtr<UBorder> LabPanel;
	struct FRowW { ESLLab Which; bool bRadio; TObjectPtr<UBorder> Mark; TObjectPtr<UTextBlock> Name; TObjectPtr<UTextBlock> Scope; };
	TArray<FRowW> Rows;
	// hud
	UPROPERTY() TObjectPtr<UTextBlock> Objective;
	UPROPERTY() TObjectPtr<UTextBlock> RoundText;
	UPROPERTY() TObjectPtr<UTextBlock> HealthText;
	UPROPERTY() TObjectPtr<UBorder> HealthBar;
	UPROPERTY() TObjectPtr<UTextBlock> AmmoText;
	UPROPERTY() TObjectPtr<UTextBlock> SmokeCount;
	UPROPERTY() TObjectPtr<UTextBlock> HECount;
	UPROPERTY() TObjectPtr<UBorder> Exposure;
	UPROPERTY() TObjectPtr<UTextBlock> ExposureText;
	UPROPERTY() TObjectPtr<UBorder> PlantBox;
	UPROPERTY() TObjectPtr<UBorder> PlantFill;
	UPROPERTY() TObjectPtr<UTextBlock> PlantText;
	UPROPERTY() TObjectPtr<UTextBlock> Readout;
	UPROPERTY() TObjectPtr<UTextBlock> NetReadout;
	UPROPERTY() TObjectPtr<UTextBlock> FeedText;
	UPROPERTY() TObjectPtr<UBorder> Banner;
	UPROPERTY() TObjectPtr<UTextBlock> BannerTitle;
	UPROPERTY() TObjectPtr<UTextBlock> BannerBody;
	UPROPERTY() TObjectPtr<UBorder> HelpCard;
	UPROPERTY() TObjectPtr<UCanvasPanel> Cross;
	UPROPERTY() TArray<TObjectPtr<UBorder>> CrossBars;
	UPROPERTY() TObjectPtr<UTextBlock> HitMark;
	UPROPERTY() TObjectPtr<UCanvasPanel> LabelLayer;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> LabelPool;
	UPROPERTY() TObjectPtr<UTextBlock> UseHint;
	float Refresh = 0.f;
};
