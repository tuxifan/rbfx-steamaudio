//
// Copyright (c) 2008-2022 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include <EASTL/sort.h>

#include "../Core/Context.h"
#include "../Core/Profiler.h"
#include "../Core/WorkQueue.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Geometry.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/Material.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/Technique.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/VertexBuffer.h"
#include "../Graphics/View.h"
#include "../IO/Log.h"
#include "../RenderPipeline/RenderPipeline.h"
#include "../Scene/Node.h"
#include "../Scene/Scene.h"
#include "../Urho2D/Drawable2D.h"
#include "../Urho2D/Renderer2D.h"

#include "../DebugNew.h"

namespace Urho3D
{

static const unsigned MASK_VERTEX2D = MASK_POSITION | MASK_COLOR | MASK_TEXCOORD1;

ViewBatchInfo2D::ViewBatchInfo2D() :
    vertexBufferUpdateFrameNumber_(0),
    indexCount_(0),
    vertexCount_(0),
    batchUpdatedFrameNumber_(0),
    batchCount_(0)
{
}

Renderer2D::Renderer2D(Context* context) :
    Drawable(context, DRAWABLE_GEOMETRY),
    material_(MakeShared<Material>(context)),
    indexBuffer_(MakeShared<IndexBuffer>(context_)),
    viewMask_(DEFAULT_VIEWMASK)
{
    material_->SetName("Urho2D");

    auto tech = MakeShared<Technique>(context_);
    Pass* pass = tech->CreatePass("alpha");
    pass->SetVertexShader("Urho2D");
    pass->SetPixelShader("Urho2D");
    pass->SetDepthWrite(false);
    cachedTechniques_[BLEND_REPLACE] = tech;

    material_->SetTechnique(0, tech);
    material_->SetCullMode(CULL_NONE);

    frame_.frameNumber_ = 0;
    SubscribeToEvent(E_BEGINVIEWUPDATE, URHO3D_HANDLER(Renderer2D, HandleBeginViewUpdate));
}

Renderer2D::~Renderer2D() = default;

void Renderer2D::RegisterObject(Context* context)
{
    context->AddFactoryReflection<Renderer2D>();
}

static inline bool CompareRayQueryResults(const RayQueryResult& lr, const RayQueryResult& rr)
{
    auto* lhs = static_cast<Drawable2D*>(lr.drawable_);
    auto* rhs = static_cast<Drawable2D*>(rr.drawable_);
    if (lhs->GetLayer() != rhs->GetLayer())
        return lhs->GetLayer() > rhs->GetLayer();

    if (lhs->GetOrderInLayer() != rhs->GetOrderInLayer())
        return lhs->GetOrderInLayer() > rhs->GetOrderInLayer();

    return lhs->GetID() > rhs->GetID();
}

void Renderer2D::ProcessRayQuery(const RayOctreeQuery& query, ea::vector<RayQueryResult>& results)
{
    unsigned resultSize = results.size();
    for (unsigned i = 0; i < drawables_.size(); ++i)
    {
        if (drawables_[i]->GetViewMask() & query.viewMask_)
            drawables_[i]->ProcessRayQuery(query, results);
    }

    if (results.size() != resultSize)
        ea::quick_sort(results.begin() + resultSize, results.end(), CompareRayQueryResults);
}

void Renderer2D::UpdateBatches(const FrameInfo& frame)
{
    unsigned count = batches_.size();

    // Update non-thread critical parts of the source batches
    for (unsigned i = 0; i < count; ++i)
    {
        batches_[i].distance_ = 10.0f + (count - i) * 0.001f;
        batches_[i].worldTransform_ = &Matrix3x4::IDENTITY;
    }

    RequestUpdateBatchesDelayed(frame);
}

void Renderer2D::UpdateBatchesDelayed(const FrameInfo& frame)
{
    unsigned indexCount = 0;
    for (auto i = viewBatchInfos_.begin(); i !=
        viewBatchInfos_.end(); ++i)
    {
        if (i->second.batchUpdatedFrameNumber_ == frame_.frameNumber_)
            indexCount = Max(indexCount, i->second.indexCount_);
    }

    // Fill index buffer
    if (indexBuffer_->GetIndexCount() < indexCount || indexBuffer_->IsDataLost())
    {
        bool largeIndices = (indexCount * 4 / 6) > 0xffff;
        indexBuffer_->SetSize(indexCount, largeIndices);

        void* buffer = indexBuffer_->Lock(0, indexCount, true);
        if (buffer)
        {
            unsigned quadCount = indexCount / 6;
            if (largeIndices)
            {
                auto* dest = reinterpret_cast<unsigned*>(buffer);
                for (unsigned i = 0; i < quadCount; ++i)
                {
                    unsigned base = i * 4;
                    dest[0] = base;
                    dest[1] = base + 1;
                    dest[2] = base + 2;
                    dest[3] = base;
                    dest[4] = base + 2;
                    dest[5] = base + 3;
                    dest += 6;
                }
            }
            else
            {
                auto* dest = reinterpret_cast<unsigned short*>(buffer);
                for (unsigned i = 0; i < quadCount; ++i)
                {
                    unsigned base = i * 4;
                    dest[0] = (unsigned short)(base);
                    dest[1] = (unsigned short)(base + 1);
                    dest[2] = (unsigned short)(base + 2);
                    dest[3] = (unsigned short)(base);
                    dest[4] = (unsigned short)(base + 2);
                    dest[5] = (unsigned short)(base + 3);
                    dest += 6;
                }
            }

            indexBuffer_->Unlock();
        }
        else
        {
            URHO3D_LOGERROR("Failed to lock index buffer");
            return;
        }
    }

    Camera* camera = frame.camera_;
    ViewBatchInfo2D& viewBatchInfo = viewBatchInfos_[camera];

    if (viewBatchInfo.vertexBufferUpdateFrameNumber_ != frame_.frameNumber_)
    {
        unsigned vertexCount = viewBatchInfo.vertexCount_;
        VertexBuffer* vertexBuffer = viewBatchInfo.vertexBuffer_;
#ifdef URHO3D_DEBUG
        vertexBuffer->SetDebugName("Renderer2D");
#endif
        if (vertexBuffer->GetVertexCount() < vertexCount)
            vertexBuffer->SetSize(vertexCount, MASK_VERTEX2D, true);

        if (vertexCount)
        {
            auto* dest = reinterpret_cast<Vertex2D*>(vertexBuffer->Lock(0, vertexCount, true));
            if (dest)
            {
                const ea::vector<const SourceBatch2D*>& sourceBatches = viewBatchInfo.sourceBatches_;
                for (unsigned b = 0; b < sourceBatches.size(); ++b)
                {
                    const ea::vector<Vertex2D>& vertices = sourceBatches[b]->vertices_;
                    for (unsigned i = 0; i < vertices.size(); ++i)
                        dest[i] = vertices[i];
                    dest += vertices.size();
                }

                vertexBuffer->Unlock();
            }
            else
                URHO3D_LOGERROR("Failed to lock vertex buffer");
        }

        viewBatchInfo.vertexBufferUpdateFrameNumber_ = frame_.frameNumber_;
    }
}

void Renderer2D::AddDrawable(Drawable2D* drawable)
{
    if (!drawable)
        return;

    drawables_.push_back(drawable);
}

void Renderer2D::RemoveDrawable(Drawable2D* drawable)
{
    if (!drawable)
        return;

    drawables_.erase_first(drawable);
}

Material* Renderer2D::GetMaterial(Texture2D* texture, BlendMode blendMode)
{
    if (!texture)
        return material_;

    auto t = cachedMaterials_.find(
        texture);
    if (t == cachedMaterials_.end())
    {
        SharedPtr<Material> newMaterial = CreateMaterial(texture, blendMode);
        cachedMaterials_[texture][blendMode] = newMaterial;
        return newMaterial;
    }

    ea::unordered_map<int, SharedPtr<Material> >& materials = t->second;
    auto b = materials.find(blendMode);
    if (b != materials.end())
        return b->second;

    SharedPtr<Material> newMaterial = CreateMaterial(texture, blendMode);
    materials[blendMode] = newMaterial;

    return newMaterial;
}

bool Renderer2D::CheckVisibility(Drawable2D* drawable) const
{
    if ((viewMask_ & drawable->GetViewMask()) == 0)
        return false;

    const BoundingBox& box = drawable->GetWorldBoundingBox();
    return frustum_.IsInsideFast(box) != OUTSIDE;
}

void Renderer2D::OnWorldBoundingBoxUpdate()
{
    // Set a large dummy bounding box to ensure the renderer is rendered
    boundingBox_.Define(-M_LARGE_VALUE, M_LARGE_VALUE);
    worldBoundingBox_ = boundingBox_;
}

SharedPtr<Material> Renderer2D::CreateMaterial(Texture2D* texture, BlendMode blendMode)
{
    SharedPtr<Material> newMaterial = material_->Clone();

    auto techIt = cachedTechniques_.find((int) blendMode);
    if (techIt == cachedTechniques_.end())
    {
        SharedPtr<Technique> tech(MakeShared<Technique>(context_));
        Pass* pass = tech->CreatePass("alpha");
        pass->SetVertexShader("Urho2D");
        pass->SetPixelShader("Urho2D");
        pass->SetDepthWrite(false);
        pass->SetBlendMode(blendMode);
        techIt = cachedTechniques_.insert(ea::make_pair((int) blendMode, tech)).first;
    }

    newMaterial->SetTechnique(0, techIt->second.Get());
    newMaterial->SetName(texture->GetName() + "_" + blendModeNames[blendMode]);
    newMaterial->SetTexture(TU_DIFFUSE, texture);

    return newMaterial;
}

void Renderer2D::HandleBeginViewUpdate(StringHash eventType, VariantMap& eventData)
{
    using namespace BeginViewUpdate;

    // Check that we are updating the correct scene
    if (GetScene() != eventData[P_SCENE].GetPtr())
        return;

    auto view = static_cast<View*>(eventData[P_VIEW].GetPtr());
    auto renderPipelineView = static_cast<RenderPipelineView*>(eventData[P_RENDERPIPELINEVIEW].GetPtr());
    frame_ = renderPipelineView ? renderPipelineView->GetFrameInfo() : view->GetFrameInfo();

    URHO3D_PROFILE("UpdateRenderer2D");

    auto* camera = static_cast<Camera*>(eventData[P_CAMERA].GetPtr());
    frustum_ = camera->GetFrustum();
    viewMask_ = camera->GetViewMask();

    // Check visibility
    {
        URHO3D_PROFILE("CheckDrawableVisibility");

        auto* queue = GetSubsystem<WorkQueue>();
        ForEachParallel(queue, drawables_, [this](unsigned, Drawable2D* drawable)
        {
            if (CheckVisibility(drawable))
                drawable->MarkInView(frame_);
        });
    }

    ViewBatchInfo2D& viewBatchInfo = viewBatchInfos_[camera];

    // Create vertex buffer
    if (!viewBatchInfo.vertexBuffer_)
        viewBatchInfo.vertexBuffer_ = MakeShared<VertexBuffer>(context_);

    UpdateViewBatchInfo(viewBatchInfo, camera);

    // Go through the drawables to form geometries & batches and calculate the total vertex / index count,
    // but upload the actual vertex data later. The idea is that the View class copies our batch vector to
    // its internal data structures, so we can reuse the batches for each view, provided that unique Geometry
    // objects are used for each view to specify the draw ranges
    batches_.resize(viewBatchInfo.batchCount_);
    for (unsigned i = 0; i < viewBatchInfo.batchCount_; ++i)
    {
        batches_[i].distance_ = viewBatchInfo.distances_[i];
        batches_[i].material_ = viewBatchInfo.materials_[i];
        batches_[i].geometry_ = viewBatchInfo.geometries_[i];
    }
}

void Renderer2D::GetDrawables(ea::vector<Drawable2D*>& drawables, Node* node)
{
    if (!node || !node->IsEnabled())
        return;

    const ea::vector<SharedPtr<Component> >& components = node->GetComponents();
    for (auto i = components.begin(); i != components.end(); ++i)
    {
        auto* drawable = dynamic_cast<Drawable2D*>(i->Get());
        if (drawable && drawable->IsEnabled())
            drawables.push_back(drawable);
    }

    const ea::vector<SharedPtr<Node> >& children = node->GetChildren();
    for (auto i = children.begin(); i != children.end(); ++i)
        GetDrawables(drawables, i->Get());
}

static inline bool CompareSourceBatch2Ds(const SourceBatch2D* lhs, const SourceBatch2D* rhs)
{
    if (lhs->drawOrder_ != rhs->drawOrder_)
        return lhs->drawOrder_ < rhs->drawOrder_;

    if (lhs->distance_ != rhs->distance_)
        return lhs->distance_ > rhs->distance_;

    if (lhs->material_ != rhs->material_)
        return lhs->material_->GetNameHash() < rhs->material_->GetNameHash();

    return lhs < rhs;
}

void Renderer2D::UpdateViewBatchInfo(ViewBatchInfo2D& viewBatchInfo, Camera* camera)
{
    // Already update in same frame
    if (viewBatchInfo.batchUpdatedFrameNumber_ == frame_.frameNumber_)
        return;

    ea::vector<const SourceBatch2D*>& sourceBatches = viewBatchInfo.sourceBatches_;
    sourceBatches.clear();
    for (unsigned d = 0; d < drawables_.size(); ++d)
    {
        if (!drawables_[d]->IsInView(camera))
            continue;

        const ea::vector<SourceBatch2D>& batches = drawables_[d]->GetSourceBatches();
        for (unsigned b = 0; b < batches.size(); ++b)
        {
            if (batches[b].material_ && !batches[b].vertices_.empty())
                sourceBatches.push_back(&batches[b]);
        }
    }

    for (unsigned i = 0; i < sourceBatches.size(); ++i)
    {
        const SourceBatch2D* sourceBatch = sourceBatches[i];
        // if (sourceBatch->owner_)
        {
            Vector3 worldPos = sourceBatch->owner_->GetNode()->GetWorldPosition();
            sourceBatch->distance_ = camera->GetDistance(worldPos);
        }
    }

    ea::quick_sort(sourceBatches.begin(), sourceBatches.end(), CompareSourceBatch2Ds);

    viewBatchInfo.batchCount_ = 0;
    Material* currMaterial = nullptr;
    unsigned iStart = 0;
    unsigned iCount = 0;
    unsigned vStart = 0;
    unsigned vCount = 0;
    float distance = M_INFINITY;

    for (unsigned b = 0; b < sourceBatches.size(); ++b)
    {
        distance = Min(distance, sourceBatches[b]->distance_);
        Material* material = sourceBatches[b]->material_;
        const ea::vector<Vertex2D>& vertices = sourceBatches[b]->vertices_;

        // When new material encountered, finish the current batch and start new
        if (currMaterial != material)
        {
            if (currMaterial)
            {
                AddViewBatch(viewBatchInfo, currMaterial, iStart, iCount, vStart, vCount, distance);
                iStart += iCount;
                iCount = 0;
                vStart += vCount;
                vCount = 0;
                distance = M_INFINITY;
            }

            currMaterial = material;
        }

        iCount += vertices.size() * 6 / 4;
        vCount += vertices.size();
    }

    // Add the final batch if necessary
    if (currMaterial && vCount)
        AddViewBatch(viewBatchInfo, currMaterial, iStart, iCount, vStart, vCount,distance);

    viewBatchInfo.indexCount_ = iStart + iCount;
    viewBatchInfo.vertexCount_ = vStart + vCount;
    viewBatchInfo.batchUpdatedFrameNumber_ = frame_.frameNumber_;
}

void Renderer2D::AddViewBatch(ViewBatchInfo2D& viewBatchInfo, Material* material,
    unsigned indexStart, unsigned indexCount, unsigned vertexStart, unsigned vertexCount, float distance)
{
    if (!material || indexCount == 0 || vertexCount == 0)
        return;

    if (viewBatchInfo.distances_.size() <= viewBatchInfo.batchCount_)
        viewBatchInfo.distances_.resize(viewBatchInfo.batchCount_ + 1);
    viewBatchInfo.distances_[viewBatchInfo.batchCount_] = distance;

    if (viewBatchInfo.materials_.size() <= viewBatchInfo.batchCount_)
        viewBatchInfo.materials_.resize(viewBatchInfo.batchCount_ + 1);
    viewBatchInfo.materials_[viewBatchInfo.batchCount_] = material;

    // Allocate new geometry if necessary
    if (viewBatchInfo.geometries_.size() <= viewBatchInfo.batchCount_)
    {
        SharedPtr<Geometry> geometry(MakeShared<Geometry>(context_));
        geometry->SetIndexBuffer(indexBuffer_);
        geometry->SetVertexBuffer(0, viewBatchInfo.vertexBuffer_);

        viewBatchInfo.geometries_.push_back(geometry);
    }

    Geometry* geometry = viewBatchInfo.geometries_[viewBatchInfo.batchCount_];
    geometry->SetDrawRange(TRIANGLE_LIST, indexStart, indexCount, vertexStart, vertexCount, false);

    viewBatchInfo.batchCount_++;
}

}
