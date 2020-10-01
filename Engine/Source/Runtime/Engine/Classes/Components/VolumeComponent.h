#pragma once

#include "CoreMinimal.h"
#include "MeshComponent.h"
#include "PrimitiveSceneProxy.h"
#include "StaticMeshResources.h"
#include "VolumeComponent.generated.h"

class AController;
class UMaterialInterface;
class UPrimitiveComponent;
class UTexture;
struct FCollisionShape;
struct FConvexVolume;
struct FEngineShowFlags;
struct FNavigableGeometryExport;

class FVolumeFieldMeshData
{
public:
	FVolumeFieldMeshData();
	~FVolumeFieldMeshData();

	void Init();

	FStaticMeshVertexBuffers* GetVertexBuffer();
	FDynamicMeshIndexBuffer16* GetIndexBuffer();
	FLocalVertexFactory* GetVertexFactory();

protected:

	FStaticMeshVertexBuffers MeshVertexBuffer;
	FDynamicMeshIndexBuffer16 IndexBuffer;

	FLocalVertexFactory VertexFactory;
};

class FVolumeSceneProxy : public FPrimitiveSceneProxy
{
public:
	FVolumeSceneProxy(const UPrimitiveComponent* Incomponent, FName ResourceName = NAME_None);
	virtual ~FVolumeSceneProxy();
public:

	virtual bool CanBeOccluded() const override { return false; }

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, 
		const FSceneViewFamily& ViewFamily, 
		uint32 VisibilityMap, 
		class FMeshElementCollector& Collector) const override;

	virtual SIZE_T GetTypeHash() const override { return sizeof(FVolumeSceneProxy); }
	virtual uint32 GetMemoryFootprint() const { return 0; }

protected:
	FVolumeFieldMeshData* VolumeMeshData;
};


UCLASS(Blueprintable, 
	ClassGroup = (Rendering, Common), 
	hidecategories = (Object, Activation, "Components|Activation"), 
	ShowCategories = (Mobility), 
	editinlinenew, 
	meta = (BlueprintSpawnableComponent))
class ENGINE_API UVolumeComponent : public UMeshComponent
{
	GENERATED_BODY()


public:
	UVolumeComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get())
		: UMeshComponent(ObjectInitializer)
	{

	}

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

protected:
};