/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinTargeterVisualComponent.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Optional visual helpers for generic targeter previews.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Targeter/SeinTargeterPreviewContext.h"
#include "SeinTargeterVisualComponent.generated.h"

class UMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UDecalComponent;

/** Optional materials per targeting result. Missing warning/blocked materials use Valid. */
USTRUCT(BlueprintType)
struct SEINARTSFRAMEWORK_API FSeinPreviewMaterials
{
	GENERATED_BODY()
	/** Material used while the target is valid. Empty preserves the component's material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS") TObjectPtr<UMaterialInterface> Valid;
	/** Material used for an allowed target with a warning. Empty uses Valid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS") TObjectPtr<UMaterialInterface> Warning;
	/** Material used for a blocked target. Empty uses Valid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS") TObjectPtr<UMaterialInterface> Blocked;
	UMaterialInterface* Resolve(ESeinTargeterValidity State) const;
	bool IsEmpty() const { return !Valid && !Warning && !Blocked; }
};

/** Base for optional render components driven by a Targeter Preview. */
UCLASS(Abstract)
class SEINARTSFRAMEWORK_API USeinTargeterVisualComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	virtual void InitializeVisual(const FSeinTargeterPreviewContext& Context) {}
	virtual void UpdateVisual(const FSeinTargeterPreviewContext& Context) {}
};

/** Mirrors authored actor meshes or displays one explicit mesh, independently of the capture gesture. */
UCLASS(ClassGroup=(SeinARTS), meta=(BlueprintSpawnableComponent))
class SEINARTSFRAMEWORK_API USeinTargeterMeshComponent : public USeinTargeterVisualComponent
{
	GENERATED_BODY()
public:
	/** Optional visual actor source. Empty uses the context's placement actor class. Does not change validation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS") TSoftClassPtr<AActor> SourceClass;
	/** Optional single mesh, overriding the actor mesh source for presentation only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS") TSoftObjectPtr<UStaticMesh> MeshOverride;
	/** Materials applied to every slot of the generated meshes, with no parameter-name convention. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS", meta=(ShowOnlyInnerProperties))
	FSeinPreviewMaterials Materials;
	/** Meshes created for this preview. Available during Preview Initialized. */
	UPROPERTY(Transient, BlueprintReadOnly, Category="SeinARTS") TArray<TObjectPtr<UMeshComponent>> GeneratedMeshes;
	virtual void InitializeVisual(const FSeinTargeterPreviewContext& Context) override;
	virtual void UpdateVisual(const FSeinTargeterPreviewContext& Context) override;
	void BindLegacy(UStaticMeshComponent* Mesh, UMaterialInterface* Material);
	/** Root mesh used for an explicit mesh override or authored fallback. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Targeter")
	UStaticMeshComponent* GetRootMesh() const { return HologramMesh; }
private:
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> HologramMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> GhostMaterial;
	bool bInitialized = false;
	bool bAppliedMaterial = false;
	ESeinTargeterValidity AppliedValidity = ESeinTargeterValidity::Valid;
	void BuildHologramMeshes();
	void ApplyGhostMaterialToComponent(UMeshComponent* Comp);
	void TintComponent(UMeshComponent* Comp, const FLinearColor& Color);
};

/** Optional area decal. Material and sizing choices belong to this component, not the capture spec. */
UCLASS(ClassGroup=(SeinARTS), meta=(BlueprintSpawnableComponent))
class SEINARTSFRAMEWORK_API USeinTargeterDecalComponent : public USeinTargeterVisualComponent
{
	GENERATED_BODY()
public:
	/** Materials used for valid, warning, and blocked targets. Use decal-domain materials. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS", meta=(ShowOnlyInnerProperties))
	FSeinPreviewMaterials Materials;
	/** Use the ability area radius when positive; otherwise use Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS") bool bUseAreaRadius = true;
	/** Fixed or fallback decal radius in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS", meta=(ClampMin="0")) float Radius = 60;
	/** Projection half-depth in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SeinARTS", meta=(ClampMin="0")) float ProjectionDepth = 200;
	/** Actual decal, available during Preview Initialized for custom animation. */
	UPROPERTY(Transient, BlueprintReadOnly, Category="SeinARTS") TObjectPtr<UDecalComponent> Decal;
	virtual void InitializeVisual(const FSeinTargeterPreviewContext& Context) override;
	virtual void UpdateVisual(const FSeinTargeterPreviewContext& Context) override;
	void BindLegacy(UDecalComponent* InDecal) { Decal = InDecal; bLegacyTint = true; }
private:
	bool bInitialized = false;
	bool bAppliedMaterial = false;
	bool bLegacyTint = false;
	ESeinTargeterValidity AppliedValidity = ESeinTargeterValidity::Valid;
};
