#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SLLevel.generated.h"

class ASLDoor;
class UMaterialInterface;

/** A smoke-permeable grate (actor tag "SmokePass": blocks bullets and players, not smoke). */
UCLASS()
class SMOKELAB_API ASLGrate : public AActor
{
	GENERATED_BODY()
public:
	ASLGrate();
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Mesh;
};

/** Named places the game mode and the self-test use. */
struct FSLLevelSpots
{
	FVector PlayerStart = FVector(500, 500, 100);
	FVector PlayerStart2 = FVector(500, 900, 100);
	TArray<FTransform> Sentries;
	FBox PlantZone;
	FVector CorridorMouth, CorridorDoor, MidLane, ThinWall, GrateWindow, SiteWindow, Stairs;
};

/**
 * Builds the drill map out of boxes at BeginPlay on every machine (deterministic, not replicated):
 * a courtyard (attacker spawn), a long roofed corridor with a door and a grate, a connector room,
 * an open mid lane and the bomb-site building with a window, an arch and a stair platform.
 */
UCLASS()
class SMOKELAB_API ASLLevel : public AActor
{
	GENERATED_BODY()
public:
	ASLLevel();
	virtual void BeginPlay() override;
	static ASLLevel* Get(const UObject* Ctx);
	static FBox VoxelBounds() { return FBox(FVector(-200, -200, 0), FVector(5000, 3400, 600)); }
	bool IsBuilt() const { return bBuilt; }
	const FSLLevelSpots& Spots() const { return S; }
	ASLDoor* GetDoor() const { return Door.Get(); }
	void SpawnServerActors();
	UPROPERTY() TObjectPtr<class UDirectionalLightComponent> SunLight;
	UPROPERTY() TArray<TObjectPtr<class UPointLightComponent>> Lamps;

private:
	bool bBuilt = false;
	FSLLevelSpots S;
	TWeakObjectPtr<ASLDoor> Door;
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Mats;
	class UStaticMeshComponent* Box(const FVector& Min, const FVector& Max, int32 Mat, bool bCollide = true);
	class UStaticMeshComponent* Mesh(class UStaticMesh* M, const FTransform& Xf, int32 Mat, bool bCollide = true);
	void Prop(const TCHAR* Name, const FVector& At, float Yaw, float Scale = 1.f, bool bCollide = true);
	void Lamp(const FVector& At, float Intensity);
	void Wall(const FVector2D& A, const FVector2D& B, float Thick, float Z0, float Z1, int32 Mat, const TArray<FVector4>& Gaps = {});
	void Build();
	void Light();
};
