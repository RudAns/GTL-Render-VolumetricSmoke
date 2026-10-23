#include "SLHud.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EngineUtils.h"
#include "SLBot.h"
#include "SLCharacter.h"
#include "SLGame.h"
#include "SLSmoke.h"
#include "SLSmokeManager.h"
#include "Styling/CoreStyle.h"

namespace
{
	// graphite panels, smoke-teal accent; amber marks switches that act on the server (everyone sees them)
	const FLinearColor Fill(0.018f, 0.022f, 0.026f, 0.84f);
	const FLinearColor FillSoft(0.02f, 0.024f, 0.03f, 0.66f);
	const FLinearColor Rim(0.42f, 0.50f, 0.54f, 0.55f);
	const FLinearColor Ink(0.94f, 0.95f, 0.93f);
	const FLinearColor Dim(0.62f, 0.67f, 0.69f);
	const FLinearColor Teal(0.38f, 0.86f, 0.84f);
	const FLinearColor Amber(1.0f, 0.72f, 0.28f);
	const FLinearColor Good(0.45f, 0.90f, 0.50f);
	const FLinearColor Bad(1.0f, 0.38f, 0.30f);
	const FLinearColor Off(0.30f, 0.33f, 0.35f, 1.f);
	const FLinearColor CapFill(0.88f, 0.89f, 0.86f);
	const FLinearColor CapInk(0.05f, 0.05f, 0.06f);

	FSlateBrush Rounded(FLinearColor F, float R, FLinearColor O = FLinearColor::Transparent, float W = 0.f)
	{
		FSlateRoundedBoxBrush B(F, R);
		B.OutlineSettings = FSlateBrushOutlineSettings(FVector4(R, R, R, R), FSlateColor(O), W);
		return B;
	}
	FText T(const FString& S) { return FText::FromString(S); }
}

void USLClick::OnClicked()
{
	if (ASLPlayerController* P = Owner.Get()) P->Lab(Which);
}

ASLPlayerController* USLHud::PC() const { return Cast<ASLPlayerController>(GetOwningPlayer()); }

UTextBlock* USLHud::Text(int32 Size, FLinearColor Color, bool bBold, float Wrap)
{
	UTextBlock* B = WidgetTree->ConstructWidget<UTextBlock>();
	B->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size));
	B->SetColorAndOpacity(FSlateColor(Color));
	B->SetShadowOffset(FVector2D(1.f, 1.f));
	B->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	if (Wrap > 0.f) { B->SetAutoWrapText(false); B->SetWrapTextAt(Wrap); }
	return B;
}

UBorder* USLHud::Panel(FLinearColor F, FLinearColor R, float Radius, FMargin Pad)
{
	UBorder* B = WidgetTree->ConstructWidget<UBorder>();
	B->SetBrush(Rounded(F, Radius, R, R.A > 0.f ? 1.2f : 0.f));
	B->SetPadding(Pad);
	return B;
}

UBorder* USLHud::KeyCap(const FString& Key, int32 Size)
{
	UBorder* Cap = Panel(CapFill, FLinearColor(0.2f, 0.22f, 0.24f, 1.f), 4.f, FMargin(7, 1, 7, 2));
	UTextBlock* Tx = Text(Size, CapInk, true);
	Tx->SetShadowColorAndOpacity(FLinearColor::Transparent);
	Tx->SetText(T(Key));
	Cap->SetContent(Tx);
	Cap->SetHorizontalAlignment(HAlign_Center);
	return Cap;
}

UCanvasPanelSlot* USLHud::Put(UWidget* W, FVector2D Anchor, FVector2D Align, FVector2D Pos)
{
	UCanvasPanelSlot* S = Canvas->AddChildToCanvas(W);
	S->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
	S->SetAlignment(Align);
	S->SetPosition(Pos);
	S->SetAutoSize(true);
	return S;
}

UButton* USLHud::Click(ESLLab Which, UWidget* Content)
{
	UButton* B = WidgetTree->ConstructWidget<UButton>();
	FButtonStyle St;
	St.SetNormal(Rounded(FLinearColor(0, 0, 0, 0), 5.f));
	St.SetHovered(Rounded(FLinearColor(0.6f, 0.9f, 0.9f, 0.10f), 5.f, Teal, 1.f));
	St.SetPressed(Rounded(FLinearColor(0.6f, 0.9f, 0.9f, 0.20f), 5.f, Teal, 1.f));
	St.SetNormalPadding(FMargin(0)); St.SetPressedPadding(FMargin(0));
	B->SetStyle(St);
	USLClick* H = NewObject<USLClick>(this);
	H->Which = Which; H->Owner = PC();
	B->OnClicked.AddDynamic(H, &USLClick::OnClicked);
	Clicks.Add(H);
	B->SetContent(Content);
	if (UButtonSlot* BS = Cast<UButtonSlot>(Content->Slot)) { BS->SetHorizontalAlignment(HAlign_Fill); BS->SetPadding(FMargin(0)); }
	return B;
}

void USLHud::Group(UVerticalBox* Box, const FString& Title, const FString& Sub)
{
	UVerticalBox* V = WidgetTree->ConstructWidget<UVerticalBox>();
	UTextBlock* A = Text(15, Teal, true); A->SetText(T(Title));
	UTextBlock* B = Text(11, Dim, false, 320.f); B->SetText(T(Sub));
	V->AddChildToVerticalBox(A);
	V->AddChildToVerticalBox(B);
	Box->AddChildToVerticalBox(V)->SetPadding(FMargin(2, 10, 0, 3));
}

void USLHud::SubHead(UVerticalBox* Box, const FString& Title, int32 Indent)
{
	// a small caption that names one set of switches, e.g. which radios exclude each other
	UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
	USpacer* Ind = WidgetTree->ConstructWidget<USpacer>(); Ind->SetSize(FVector2D(8.f + Indent * 22.f, 1));
	H->AddChildToHorizontalBox(Ind);
	UTextBlock* A = Text(11, Dim, true); A->SetText(T(Title));
	H->AddChildToHorizontalBox(A)->SetVerticalAlignment(VAlign_Center);
	Box->AddChildToVerticalBox(H)->SetPadding(FMargin(0, 6, 0, 1));
}

void USLHud::Row(UVerticalBox* Box, ESLLab Which, bool bRadio, int32 Indent)
{
	UHorizontalBox* H = WidgetTree->ConstructWidget<UHorizontalBox>();
	USpacer* Ind = WidgetTree->ConstructWidget<USpacer>(); Ind->SetSize(FVector2D(6.f + Indent * 22.f, 1));
	H->AddChildToHorizontalBox(Ind);
	if (Indent > 0)
	{
		UTextBlock* Tree = Text(12, Dim); Tree->SetText(T(TEXT("└ ")));
		H->AddChildToHorizontalBox(Tree)->SetVerticalAlignment(VAlign_Center);
	}
	UBorder* Mark = Panel(FLinearColor::Transparent, Ink, bRadio ? 7.f : 2.5f, FMargin(0));
	USizeBox* MB = WidgetTree->ConstructWidget<USizeBox>(); MB->SetWidthOverride(14.f); MB->SetHeightOverride(14.f); MB->SetContent(Mark);
	H->AddChildToHorizontalBox(MB)->SetVerticalAlignment(VAlign_Center);
	USpacer* G = WidgetTree->ConstructWidget<USpacer>(); G->SetSize(FVector2D(8, 1));
	H->AddChildToHorizontalBox(G);
	UTextBlock* Name = Text(14, Ink); Name->SetText(ASLPlayerController::LabName(Which));
	H->AddChildToHorizontalBox(Name)->SetVerticalAlignment(VAlign_Center);
	USpacer* Fl = WidgetTree->ConstructWidget<USpacer>();
	H->AddChildToHorizontalBox(Fl)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	const FString Key = ASLPlayerController::LabKey(Which);
	if (!Key.IsEmpty()) H->AddChildToHorizontalBox(KeyCap(Key, 10))->SetVerticalAlignment(VAlign_Center);
	UTextBlock* Scope = Text(10, ASLPlayerController::LabIsServer(Which) ? Amber : Dim);
	Scope->SetText(T(ASLPlayerController::LabIsServer(Which) ? TEXT("  服务器") : TEXT("  本机")));
	H->AddChildToHorizontalBox(Scope)->SetVerticalAlignment(VAlign_Center);
	UBorder* Back = Panel(FLinearColor::Transparent, FLinearColor::Transparent, 5.f, FMargin(2, 3, 6, 3));
	Back->SetContent(H);
	Back->SetToolTipText(ASLPlayerController::LabHint(Which));
	Box->AddChildToVerticalBox(Click(Which, Back));
	Rows.Add({ Which, bRadio, Mark, Name, Scope });
}

TSharedRef<SWidget> USLHud::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		auto VBox = [this]() { return WidgetTree->ConstructWidget<UVerticalBox>(); };
		auto HBox = [this]() { return WidgetTree->ConstructWidget<UHorizontalBox>(); };
		auto Label = [this](const FString& S, int32 Size, FLinearColor C, bool bBold = false, float Wrap = 0.f) { UTextBlock* Tx = Text(Size, C, bBold, Wrap); Tx->SetText(T(S)); return Tx; };
		auto Gap = [this](float W, float H) { USpacer* S = WidgetTree->ConstructWidget<USpacer>(); S->SetSize(FVector2D(W, H)); return S; };
		auto Sized = [this](UWidget* W, float Wd, float Ht) { USizeBox* B = WidgetTree->ConstructWidget<USizeBox>(); if (Wd > 0) B->SetWidthOverride(Wd); if (Ht > 0) B->SetHeightOverride(Ht); B->SetContent(W); return B; };

		// world labels (behind everything else)
		LabelLayer = WidgetTree->ConstructWidget<UCanvasPanel>();
		UCanvasPanelSlot* LS = Canvas->AddChildToCanvas(LabelLayer);
		LS->SetAnchors(FAnchors(0, 0, 1, 1)); LS->SetOffsets(FMargin(0));

		// ---------------- title + objective (top-left) ----------------
		{
			UBorder* Card = Panel(Fill, Rim, 9.f, FMargin(16, 11, 18, 12));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("烟雾演练"), 22, Ink, true));
			V->AddChildToVerticalBox(Label(TEXT("体素扩散 · 弹道挖空 · 多人一致性"), 12, Dim));
			V->AddChildToVerticalBox(Gap(1, 6));
			RoundText = Text(13, Teal, true);
			V->AddChildToVerticalBox(RoundText);
			Objective = Text(14, Ink, false, 330.f);
			V->AddChildToVerticalBox(Objective);
			Card->SetContent(V);
			Put(Card, FVector2D(0, 0), FVector2D(0, 0), FVector2D(18, 16));
		}
		// ---------------- readouts (top-right) ----------------
		{
			UBorder* Card = Panel(FillSoft, Rim, 9.f, FMargin(14, 10, 16, 11));
			UVerticalBox* V = VBox();
			V->AddChildToVerticalBox(Label(TEXT("渲染与网络读数"), 13, Teal, true));
			Readout = Text(12, Ink); V->AddChildToVerticalBox(Readout);
			V->AddChildToVerticalBox(Gap(1, 4));
			NetReadout = Text(12, Ink); V->AddChildToVerticalBox(NetReadout);
			Card->SetContent(Sized(V, 330.f, 0));
			Put(Card, FVector2D(1, 0), FVector2D(1, 0), FVector2D(-18, 16));
		}
		// ---------------- kill feed (right) ----------------
		{
			FeedText = Text(13, Ink, false, 330.f);
			FeedText->SetJustification(ETextJustify::Right);
			Put(FeedText, FVector2D(1, 0.42f), FVector2D(1, 0), FVector2D(-22, 0));
		}
		// ---------------- crosshair ----------------
		{
			Cross = WidgetTree->ConstructWidget<UCanvasPanel>();
			UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(Cross);
			CS->SetAnchors(FAnchors(0.5f, 0.5f)); CS->SetAlignment(FVector2D(0.5f, 0.5f)); CS->SetSize(FVector2D(80, 80)); CS->SetPosition(FVector2D(0, 0));
			for (int32 I = 0; I < 4; ++I)
			{
				UBorder* B = Panel(FLinearColor(0.85f, 1.f, 0.95f, 0.95f), FLinearColor(0, 0, 0, 0.7f), 0.5f, FMargin(0));
				UCanvasPanelSlot* S = Cross->AddChildToCanvas(B);
				S->SetSize(I < 2 ? FVector2D(12, 3) : FVector2D(3, 12));
				S->SetAlignment(FVector2D(0.5f, 0.5f));
				CrossBars.Add(B);
			}
			HitMark = Text(26, Bad, true); HitMark->SetText(T(TEXT("╳")));
			UCanvasPanelSlot* HS = Cross->AddChildToCanvas(HitMark);
			HS->SetAutoSize(true); HS->SetAlignment(FVector2D(0.5f, 0.5f)); HS->SetPosition(FVector2D(40, 40));
		}
		// ---------------- exposure + plant (center below crosshair) ----------------
		{
			Exposure = Panel(FLinearColor(0.1f, 0.12f, 0.12f, 0.75f), Rim, 12.f, FMargin(12, 3, 12, 4));
			ExposureText = Text(13, Ink, true);
			Exposure->SetContent(ExposureText);
			Put(Exposure, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0), FVector2D(0, 70));
			PlantBox = Panel(Fill, Rim, 6.f, FMargin(10, 6, 10, 8));
			UVerticalBox* V = VBox();
			PlantText = Text(13, Ink, true);
			V->AddChildToVerticalBox(PlantText);
			UBorder* Track = Panel(FLinearColor(0.2f, 0.22f, 0.23f, 1.f), FLinearColor::Transparent, 3.f, FMargin(0));
			UHorizontalBox* TB = HBox();
			PlantFill = Panel(Amber, FLinearColor::Transparent, 3.f, FMargin(0));
			TB->AddChildToHorizontalBox(Sized(PlantFill, 1.f, 8.f));
			Track->SetContent(Sized(TB, 260.f, 8.f));
			Track->SetHorizontalAlignment(HAlign_Left);
			V->AddChildToVerticalBox(Track)->SetPadding(FMargin(0, 5, 0, 0));
			PlantBox->SetContent(V);
			Put(PlantBox, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0), FVector2D(0, 110));
			UseHint = Text(14, Ink, true);
			Put(UseHint, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0), FVector2D(0, 170));
		}
		// ---------------- health / ammo / grenades (bottom) ----------------
		{
			UBorder* Card = Panel(Fill, Rim, 9.f, FMargin(14, 9, 16, 10));
			UHorizontalBox* H = HBox();
			UVerticalBox* HP = VBox();
			HealthText = Text(22, Ink, true);
			HP->AddChildToVerticalBox(HealthText);
			UBorder* Track = Panel(FLinearColor(0.2f, 0.22f, 0.23f, 1.f), FLinearColor::Transparent, 3.f, FMargin(0));
			UHorizontalBox* TB = HBox();
			HealthBar = Panel(Good, FLinearColor::Transparent, 3.f, FMargin(0));
			TB->AddChildToHorizontalBox(Sized(HealthBar, 160.f, 7.f));
			Track->SetContent(Sized(TB, 160.f, 7.f));
			Track->SetHorizontalAlignment(HAlign_Left);
			HP->AddChildToVerticalBox(Track)->SetPadding(FMargin(0, 3, 0, 0));
			H->AddChildToHorizontalBox(HP);
			H->AddChildToHorizontalBox(Gap(26, 1));
			UVerticalBox* Am = VBox();
			AmmoText = Text(22, Ink, true);
			Am->AddChildToVerticalBox(AmmoText);
			UHorizontalBox* Keys = HBox();
			Keys->AddChildToHorizontalBox(KeyCap(TEXT("左键"), 11)); Keys->AddChildToHorizontalBox(Label(TEXT(" 射击  "), 12, Dim));
			Keys->AddChildToHorizontalBox(KeyCap(TEXT("R"), 11)); Keys->AddChildToHorizontalBox(Label(TEXT(" 换弹"), 12, Dim));
			Am->AddChildToVerticalBox(Keys);
			H->AddChildToHorizontalBox(Am);
			H->AddChildToHorizontalBox(Gap(26, 1));
			UVerticalBox* Gr = VBox();
			UHorizontalBox* G1 = HBox();
			G1->AddChildToHorizontalBox(KeyCap(TEXT("G"), 12)); G1->AddChildToHorizontalBox(Label(TEXT("  烟雾弹 "), 14, Ink));
			SmokeCount = Text(16, Teal, true); G1->AddChildToHorizontalBox(SmokeCount);
			UHorizontalBox* G2 = HBox();
			G2->AddChildToHorizontalBox(KeyCap(TEXT("H"), 12)); G2->AddChildToHorizontalBox(Label(TEXT("  高爆雷 "), 14, Ink));
			HECount = Text(16, Amber, true); G2->AddChildToHorizontalBox(HECount);
			Gr->AddChildToVerticalBox(G1);
			Gr->AddChildToVerticalBox(G2)->SetPadding(FMargin(0, 4, 0, 0));
			H->AddChildToHorizontalBox(Gr);
			Card->SetContent(H);
			Put(Card, FVector2D(0.5f, 1), FVector2D(0.5f, 1), FVector2D(0, -18));
		}
		// ---------------- help card (bottom-left) ----------------
		{
			HelpCard = Panel(FillSoft, Rim, 9.f, FMargin(14, 9, 16, 10));
			UVerticalBox* V = VBox();
			auto KeyRow = [&](const FString& K, const FString& What)
			{
				UHorizontalBox* R = HBox();
				R->AddChildToHorizontalBox(Sized(KeyCap(K, 11), 66.f, 0));
				R->AddChildToHorizontalBox(Label(What, 13, Ink));
				V->AddChildToVerticalBox(R)->SetPadding(FMargin(0, 1, 0, 1));
			};
			KeyRow(TEXT("WASD"), TEXT("移动   空格 跳   Ctrl 蹲"));
			KeyRow(TEXT("E"), TEXT("开关门 / 在包点长按安放炸弹"));
			KeyRow(TEXT("Tab"), TEXT("原理面板（鼠标点选开关）"));
			KeyRow(TEXT("B"), TEXT("在准星处直接放一颗烟（练习）"));
			KeyRow(TEXT("V"), TEXT("显示烟雾体素"));
			KeyRow(TEXT("X"), TEXT("透视视角：本机已知的敌人"));
			KeyRow(TEXT("Enter"), TEXT("重开本回合"));
			KeyRow(TEXT("F1"), TEXT("隐藏这张说明"));
			HelpCard->SetContent(V);
			Put(HelpCard, FVector2D(0, 1), FVector2D(0, 1), FVector2D(18, -18));
		}
		// ---------------- round banner ----------------
		{
			Banner = Panel(Fill, Amber, 10.f, FMargin(26, 16, 26, 18));
			UVerticalBox* V = VBox();
			BannerTitle = Text(28, Amber, true);
			BannerBody = Text(15, Ink, false, 520.f);
			V->AddChildToVerticalBox(BannerTitle);
			V->AddChildToVerticalBox(BannerBody)->SetPadding(FMargin(0, 6, 0, 0));
			Banner->SetContent(V);
			Put(Banner, FVector2D(0.5f, 0.3f), FVector2D(0.5f, 0.5f), FVector2D(0, 0));
		}
		// ---------------- principle panel (left, Tab) ----------------
		{
			LabPanel = Panel(Fill, Rim, 10.f, FMargin(12, 10, 10, 12));
			UVerticalBox* Outer = VBox();
			UHorizontalBox* Head = HBox();
			Head->AddChildToHorizontalBox(Label(TEXT("原理面板"), 19, Ink, true));
			Head->AddChildToHorizontalBox(Gap(10, 1));
			Head->AddChildToHorizontalBox(Label(TEXT("单选圆点互斥，方框逐层叠加；"), 11, Dim))->SetVerticalAlignment(VAlign_Bottom);
			Outer->AddChildToVerticalBox(Head);
			UHorizontalBox* Legend = HBox();
			Legend->AddChildToHorizontalBox(Label(TEXT("服务器"), 11, Amber, true));
			Legend->AddChildToHorizontalBox(Label(TEXT(" = 改了所有人都受影响，"), 11, Dim));
			Legend->AddChildToHorizontalBox(Label(TEXT("本机"), 11, Ink, true));
			Legend->AddChildToHorizontalBox(Label(TEXT(" = 只改这台机器的画面"), 11, Dim));
			Outer->AddChildToVerticalBox(Legend);
			UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
			UVerticalBox* B = VBox();
			Group(B, TEXT("① 扩散规则"), TEXT("烟怎样占据空间：只看椭球、沿连通空气走、还是保持体积不变"));
			SubHead(B, TEXT("规则 · 三选一"), 0);
			Row(B, ESLLab::RuleA, true, 0);
			Row(B, ESLLab::RuleB, true, 0);
			Row(B, ESLLab::RuleC, true, 0);
			SubHead(B, TEXT("观察 · 对三种规则都有效"), 0);
			Row(B, ESLLab::SlowGrowth, false, 0);
			Row(B, ESLLab::ShowVoxels, false, 0);
			Row(B, ESLLab::ShowLevelVoxels, false, 0);
			Group(B, TEXT("② 交互"), TEXT("子弹和爆炸不改体素，着色器按事件时间挖空"));
			Row(B, ESLLab::Bullets, false, 0);
			Row(B, ESLLab::Blasts, false, 0);
			Row(B, ESLLab::Refill, false, 1);
			Group(B, TEXT("③ 渲染"), TEXT("同一份体素，不同的步进分辨率、噪声、光照与上采样"));
			Row(B, ESLLab::SmokeOff, false, 0);
			SubHead(B, TEXT("步进分辨率 · 三选一"), 1);
			Row(B, ESLLab::Res1, true, 1); Row(B, ESLLab::Res2, true, 1); Row(B, ESLLab::Res4, true, 1);
			SubHead(B, TEXT("上采样 · 低分辨率时三选一"), 2);
			Row(B, ESLLab::UpBilinear, true, 2); Row(B, ESLLab::UpDepth, true, 2); Row(B, ESLLab::UpFix, true, 2);
			SubHead(B, TEXT("噪声 · 三选一"), 1);
			Row(B, ESLLab::NoiseNone, true, 1); Row(B, ESLLab::NoiseProc, true, 1); Row(B, ESLLab::NoiseTex, true, 1);
			Row(B, ESLLab::GameplayFloor, false, 2);
			SubHead(B, TEXT("光照 · 三选一"), 1);
			Row(B, ESLLab::LightCache, true, 1); Row(B, ESLLab::LightRay, true, 1); Row(B, ESLLab::LightNone, true, 1);
			SubHead(B, TEXT("调试视图 · 三选一"), 1);
			Row(B, ESLLab::ViewSmoke, true, 1); Row(B, ESLLab::ViewHeat, true, 1); Row(B, ESLLab::ViewCache, true, 1);
			Group(B, TEXT("④ 网络同步"), TEXT("体素结果怎样到达每台机器"));
			SubHead(B, TEXT("同步方式 · 二选一"), 0);
			Row(B, ESLLab::SyncServer, true, 0);
			Row(B, ESLLab::SyncLocal, true, 0);
			Row(B, ESLLab::InjectDiff, false, 1);
			Group(B, TEXT("⑤ 公平性"), TEXT("烟挡住了画面，是否也挡住了数据"));
			Row(B, ESLLab::BotsFire, false, 0);
			Row(B, ESLLab::Sight, false, 0);
			Row(B, ESLLab::Cull, false, 0);
			Row(B, ESLLab::Wallhack, false, 1);
			Group(B, TEXT("操作"), TEXT(""));
			Row(B, ESLLab::SmokeAtCrosshair, false, 0);
			Row(B, ESLLab::Restart, false, 0);
			Scroll->AddChild(B);
			Outer->AddChildToVerticalBox(Sized(Scroll, 360.f, 600.f))->SetPadding(FMargin(0, 6, 0, 0));
			LabPanel->SetContent(Outer);
			Put(LabPanel, FVector2D(0, 0), FVector2D(0, 0), FVector2D(18, 186));
		}
	}
	return Super::RebuildWidget();
}

void USLHud::NativeTick(const FGeometry& Geo, float Dt)
{
	Super::NativeTick(Geo, Dt);
	ASLPlayerController* P = PC();
	if (!P) return;
	ASLCharacter* C = P->GetSoldier();
	ASLSmokeManager* M = P->GetManager();
	ASLGameState* GS = ASLGameState::Get(P);
	const float Now = P->GetWorld()->GetTimeSeconds();

	// world labels
	for (int32 I = LabelPool.Num(); I < Labels.Num(); ++I)
	{
		UTextBlock* L = Text(12, Ink, true);
		UCanvasPanelSlot* S = LabelLayer->AddChildToCanvas(L);
		S->SetAutoSize(true); S->SetAlignment(FVector2D(0.5f, 1.f));
		LabelPool.Add(L);
	}
	for (int32 I = 0; I < LabelPool.Num(); ++I)
	{
		UTextBlock* L = LabelPool[I];
		if (I < Labels.Num())
		{
			L->SetVisibility(ESlateVisibility::HitTestInvisible);
			L->SetText(T(Labels[I].Text));
			L->SetColorAndOpacity(FSlateColor(Labels[I].Color));
			if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(L->Slot)) S->SetPosition(Labels[I].Pos);
		}
		else L->SetVisibility(ESlateVisibility::Collapsed);
	}

	// crosshair: gap grows with spread
	if (C)
	{
		const float Gap = 7.f + FMath::Clamp(C->GetVelocity().Size2D() / 430.f, 0.f, 1.f) * 12.f + C->Recoil * 2.f;
		const FVector2D Pos[4] = { FVector2D(40 - Gap - 6, 40), FVector2D(40 + Gap + 6, 40), FVector2D(40, 40 - Gap - 6), FVector2D(40, 40 + Gap + 6) };
		for (int32 I = 0; I < 4; ++I) if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(CrossBars[I]->Slot)) S->SetPosition(Pos[I]);
		HitMark->SetVisibility(Now - C->HitMarkerTime < 0.18f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	Cross->SetVisibility(C && !C->IsDead() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	Refresh -= Dt;
	if (Refresh > 0.f) return;
	Refresh = 0.1f;

	LabPanel->SetVisibility(P->IsPanelOpen() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	HelpCard->SetVisibility(P->IsHelpOpen() && !P->IsPanelOpen() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	for (const FRowW& R : Rows)
	{
		const bool bOn = P->LabOn(R.Which);
		const bool bEn = P->LabEnabled(R.Which);
		const FLinearColor Acc = ASLPlayerController::LabIsServer(R.Which) ? Amber : Teal;
		R.Mark->SetBrush(Rounded(bOn ? (bEn ? Acc : Off) : FLinearColor::Transparent, R.bRadio ? 7.f : 2.5f, bEn ? Ink : Off, 1.5f));
		R.Name->SetColorAndOpacity(FSlateColor(bEn ? (bOn ? Ink : Dim) : Off));
	}

	// round
	if (GS)
	{
		// RoundStart/RoundEnd are server times; a client's own world clock starts when it joins
		const float Secs = FMath::Max(0.f, (GS->Round == ESLRound::Playing ? (float)GS->GetServerWorldTimeSeconds() : GS->RoundEnd) - GS->RoundStart);
		RoundText->SetText(T(FString::Printf(TEXT("第 %d 次尝试 · %d:%02d · 哨兵剩余 %d"), GS->Attempt, int32(Secs) / 60, int32(Secs) % 60, GS->BotsAlive)));
		FString Feed;
		for (const FString& S : GS->Feed) Feed += S + TEXT("\n");
		FeedText->SetText(T(Feed));
		if (GS->Round == ESLRound::Playing)
		{
			Banner->SetVisibility(ESlateVisibility::Collapsed);
			Objective->SetText(T(TEXT("三名哨兵守着长走廊、中路和包点拱门。用烟雾弹封住他们的视线，推进到 A 点，在橙色区域长按 E 安放炸弹。")));
		}
		else
		{
			Banner->SetVisibility(ESlateVisibility::HitTestInvisible);
			const bool bWin = GS->Round == ESLRound::Planted;
			BannerTitle->SetText(T(bWin ? TEXT("炸弹已安放") : TEXT("你被击倒了")));
			BannerTitle->SetColorAndOpacity(FSlateColor(bWin ? Good : Bad));
			BannerBody->SetText(T(FString::Printf(TEXT("用时 %d:%02d。按 Enter 再来一次；打开 Tab 面板换一种扩散规则或渲染方式，看看同样的投掷会有什么不同。"), int32(Secs) / 60, int32(Secs) % 60)));
		}
	}

	if (C)
	{
		HealthText->SetText(T(FString::Printf(TEXT("%d"), FMath::RoundToInt(C->Health))));
		if (USizeBox* SB = Cast<USizeBox>(HealthBar->GetParent())) SB->SetWidthOverride(FMath::Max(1.f, 160.f * C->Health / 100.f));
		HealthBar->SetBrush(Rounded(C->Health > 35.f ? Good : Bad, 3.f));
		AmmoText->SetText(T(C->ReloadLeft > 0.f ? TEXT("换弹中…") : FString::Printf(TEXT("%d / %d"), C->Ammo, C->Reserve)));
		SmokeCount->SetText(T(FString::Printf(TEXT("× %d"), C->Smokes)));
		HECount->SetText(T(FString::Printf(TEXT("× %d"), C->HEs)));
		const bool bInSmoke = M && M->DensityAt(C->EyeLocation()) > 0.3f;
		ExposureText->SetText(T(C->IsDead() ? TEXT("已阵亡") : (C->bSeenByBot ? TEXT("● 被哨兵看见") : (bInSmoke ? TEXT("烟中 · 未被发现") : TEXT("未被发现")))));
		ExposureText->SetColorAndOpacity(FSlateColor(C->bSeenByBot ? Bad : Good));
		PlantBox->SetVisibility(C->bInPlantZone || C->PlantProgress > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		PlantText->SetText(T(C->PlantProgress > 0.f ? TEXT("安放中…保持不动") : TEXT("在包点内：长按 E 安放炸弹")));
		if (USizeBox* SB = Cast<USizeBox>(PlantFill->GetParent())) SB->SetWidthOverride(FMath::Max(1.f, 260.f * C->PlantProgress));
		// door hint
		FString Hint;
		for (TActorIterator<ASLDoor> It(P->GetWorld()); It; ++It)
			if (FVector::Dist2D(It->GetActorLocation(), C->GetActorLocation()) < 240.f) Hint = It->bOpen ? TEXT("E  关门") : TEXT("E  开门");
		UseHint->SetText(T(Hint));
	}

	if (M)
	{
		const FSLGpuTimes& G = M->GpuTimes;
		const FSLRenderSettings& R = M->View.Render;
		int32 Count = 0; int32 Bytes = 0; int32 Mismatch = 0; int32 Filled = 0;
		for (const TWeakObjectPtr<ASLSmoke>& S : M->GetSmokes())
			if (S.IsValid())
			{
				++Count; Bytes += S->ReplicatedBytes; Filled += S->Field.Filled;
				if (S->FieldCrc != S->ServerFieldCrc) ++Mismatch;
			}
		static const TCHAR* ResN[] = { TEXT(""), TEXT("全分辨率"), TEXT("1/2 分辨率"), TEXT(""), TEXT("1/4 分辨率") };
		Readout->SetText(T(FString::Printf(
			TEXT("步进 %s  %d×%d\n光照缓存 %.2f ms · 步进 %.2f ms · 合成 %.2f ms\n烟雾 %d 颗 · 体素 %d 个 · 上次填充 %.0f µs"),
			ResN[FMath::Clamp(R.ResDiv, 1, 4)], G.LowW, G.LowH, G.LightMs, G.MarchMs, G.CompositeMs, Count, Filled, M->LastFillMicros)));
		const bool bServer = P->HasAuthority();
		NetReadout->SetText(T(FString::Printf(TEXT("%s · 同步：%s\n每颗烟上网 %d 字节 · 与服务器不一致 %d 颗%s"),
			bServer ? TEXT("本机是服务器") : TEXT("本机是客户端"),
			M->Settings.Sync == ESLSyncMode::ServerResult ? TEXT("服务器下发体素结果") : TEXT("只发输入，各自填充"),
			Count ? Bytes / Count : 0, Mismatch, Mismatch ? TEXT("\n画面已与服务器不同") : TEXT(""))));
		NetReadout->SetColorAndOpacity(FSlateColor(Mismatch ? Bad : Ink));
	}
}
