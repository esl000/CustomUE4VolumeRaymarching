#include "Components/VolumeComponent.h"
#include "EngineStats.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "WorldCollision.h"
#include "AI/NavigationSystemBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/WorldSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "ContentStreaming.h"
#include "DrawDebugHelpers.h"
#include "UnrealEngine.h"
#include "PhysicsPublic.h"
#include "PhysicsEngine/BodySetup.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"
#include "CollisionDebugDrawingPublic.h"
#include "GameFramework/CheatManager.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "Algo/Copy.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"
#include "EngineModule.h"
#include "SceneManagement.h"
#include "RHIStaticStates.h"
#include "RHIDefinitions.h"

//#include "Pri"

#if WITH_EDITOR
#include "Engine/LODActor.h"
#endif // WITH_EDITOR

#if DO_CHECK
#include "Engine/StaticMesh.h"
#endif

#define LOCTEXT_NAMESPACE "VolumeComponent"

CSV_DEFINE_CATEGORY(VolumeComponent, false);

FPrimitiveSceneProxy * UVolumeComponent::CreateSceneProxy()
{
	return new FVolumeSceneProxy(this);
}

FVolumeSceneProxy::FVolumeSceneProxy(const UPrimitiveComponent * Incomponent, FName ResourceName)
	: FPrimitiveSceneProxy(Incomponent, ResourceName)
{
	VolumeMeshData = new FVolumeFieldMeshData();
	VolumeMeshData->Init();
}

FVolumeSceneProxy::~FVolumeSceneProxy()
{
	delete VolumeMeshData;
}

void FVolumeSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, 
	const FSceneViewFamily & ViewFamily, 
	uint32 VisibilityMap, 
	FMeshElementCollector & Collector) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_VolumeSceneProxy_GetDynamicMeshElements);

	const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

	FColoredMaterialRenderProxy* WireframeMaterialInstance = nullptr;
	WireframeMaterialInstance = new FColoredMaterialRenderProxy(GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr, FLinearColor(0, 0.5f, 1.f));

	Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
	

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];
			const FMatrix& localToWorld = GetLocalToWorld();

			// Taking into account the min and maximum drawing distance
			const float DistanceSqr = (View->ViewMatrices.GetViewOrigin() - localToWorld.GetOrigin()).SizeSquared();
			if (DistanceSqr < FMath::Square(GetMinDrawDistance()) || DistanceSqr > FMath::Square(GetMaxDrawDistance()))
			{
				continue;
			}

			FMeshBatch& MeshBatch = Collector.AllocateMesh();
			FMeshBatchElement& BatchElement = MeshBatch.Elements[0];

			MeshBatch.VertexFactory = VolumeMeshData->GetVertexFactory();
			MeshBatch.MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();

			bool bHasPrecomputedVolumetricLightmap;
			FMatrix PreviousLocalToWorld;
			int32 SingleCaptureIndex;
			bool bOutputVelocity;

			GetScene().GetPrimitiveUniformShaderParameters_RenderThread(GetPrimitiveSceneInfo(), bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
			//Alloate a temporary primitive uniform buffer, fill it with the data and set it in the batch element
			FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
			DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), PreviousLocalToWorld, GetBounds(), GetLocalBounds(), true, bHasPrecomputedVolumetricLightmap, DrawsVelocity(), bOutputVelocity);

			BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;
			BatchElement.PrimitiveIdMode = PrimID_DynamicPrimitiveShaderData;

			BatchElement.IndexBuffer = VolumeMeshData->GetIndexBuffer();
			BatchElement.FirstIndex = 0;
			BatchElement.NumPrimitives = VolumeMeshData->GetIndexBuffer()->Indices.Num() / 3;
			BatchElement.MinVertexIndex = 0;
			BatchElement.MaxVertexIndex = VolumeMeshData->GetVertexBuffer()->PositionVertexBuffer.GetNumVertices() - 1;

			MeshBatch.Type = PT_TriangleList;
			MeshBatch.DepthPriorityGroup = SDPG_World;
			MeshBatch.CastShadow = 0;
			MeshBatch.LODIndex = 0;
			MeshBatch.bWireframe = 0;

			Collector.AddMesh(ViewIndex, MeshBatch);
		}
	}
}

FPrimitiveViewRelevance FVolumeSceneProxy::GetViewRelevance(const FSceneView * View) const
{
	const bool bVisibleForSelection = true || IsSelected();
	const bool bShowForCollision = View->Family->EngineShowFlags.Collision && IsCollisionEnabled();

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = (IsShown(View) && bVisibleForSelection) || bShowForCollision;
	Result.bDynamicRelevance = true;
	Result.bShadowRelevance = IsShadowCast(View);
	Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
	return Result;
}

FVolumeFieldMeshData::FVolumeFieldMeshData()
	: MeshVertexBuffer()
	, IndexBuffer()
	, VertexFactory(ERHIFeatureLevel::SM5, "FMeshVertexFactory")
{
}

FVolumeFieldMeshData::~FVolumeFieldMeshData()
{
	MeshVertexBuffer.PositionVertexBuffer.ReleaseResource();
	MeshVertexBuffer.StaticMeshVertexBuffer.ReleaseResource();
	MeshVertexBuffer.ColorVertexBuffer.ReleaseResource();
	VertexFactory.ReleaseResource();
	IndexBuffer.ReleaseResource();
}

void FVolumeFieldMeshData::Init()
{
	TArray<FDynamicMeshVertex> vertice = {
	FDynamicMeshVertex(FVector(-1.0, -1.0, 1.0), FVector2D::ZeroVector, FColor(0, 0, 255)),
	FDynamicMeshVertex(FVector(1.0, -1.0, 1.0), FVector2D::ZeroVector, FColor(255, 0, 255)),
	FDynamicMeshVertex(FVector(1.0, 1.0, 1.0), FVector2D::ZeroVector, FColor(255, 255, 255)),
	FDynamicMeshVertex(FVector(-1.0, 1.0, 1.0), FVector2D::ZeroVector, FColor(0, 255, 255)),
	FDynamicMeshVertex(FVector(-1.0, -1.0, -1.0), FVector2D::ZeroVector, FColor(0, 0, 0)),
	FDynamicMeshVertex(FVector(1.0, -1.0, -1.0), FVector2D::ZeroVector, FColor(255, 0, 0)),
	FDynamicMeshVertex(FVector(1.0, 1.0, -1.0), FVector2D::ZeroVector, FColor(255, 255, 0)),
	FDynamicMeshVertex(FVector(-1.0, 1.0, -1.0), FVector2D::ZeroVector, FColor(0, 255, 0))
	};

	MeshVertexBuffer.InitFromDynamicVertex(&VertexFactory, vertice);

	IndexBuffer.Indices = {
	0, 1, 2,
	2, 3, 0,
	1, 5, 6,
	6, 2, 1,
	7, 6, 5,
	5, 4, 7,
	4, 0, 3,
	3, 7, 4,
	4, 5, 1,
	1, 0, 4,
	3, 2, 6,
	6, 7, 3
	};
	
	ENQUEUE_RENDER_COMMAND(CustomIndexBuffersInit)([this](FRHICommandListImmediate& RHICmdList) {
		if (!IndexBuffer.IsInitialized())
		{
			IndexBuffer.InitResource();
		}
		else
		{
			IndexBuffer.UpdateRHI();
		}
	});

}


FStaticMeshVertexBuffers * FVolumeFieldMeshData::GetVertexBuffer()
{
	return &MeshVertexBuffer;
}

FDynamicMeshIndexBuffer16 * FVolumeFieldMeshData::GetIndexBuffer()
{
	return &IndexBuffer;
}

FLocalVertexFactory * FVolumeFieldMeshData::GetVertexFactory()
{
	return &VertexFactory;
}
