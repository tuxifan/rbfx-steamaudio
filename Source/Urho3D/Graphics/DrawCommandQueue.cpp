// Copyright (c) 2017-2020 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "Urho3D/Precompiled.h"

#include "Urho3D/Graphics/DrawCommandQueue.h"

#include "Urho3D/Graphics/Graphics.h"
#include "Urho3D/Graphics/RenderSurface.h"
#include "Urho3D/Graphics/Texture.h"
#include "Urho3D/RenderAPI/RenderAPIUtils.h"
#include "Urho3D/RenderAPI/RenderContext.h"
#include "Urho3D/RenderAPI/RenderDevice.h"

#include <Diligent/Graphics/GraphicsEngine/interface/DeviceContext.h>

#include "Urho3D/DebugNew.h"

namespace Urho3D
{

namespace
{

Diligent::VALUE_TYPE GetIndexType(RawBuffer* indexBuffer)
{
    return indexBuffer->GetStride() == 2 ? Diligent::VT_UINT16 : Diligent::VT_UINT32;
}

}

DrawCommandQueue::DrawCommandQueue(Graphics* graphics)
    : graphics_(graphics)
{
}

void DrawCommandQueue::Reset()
{
    // Reset state accumulators
    currentDrawCommand_ = {};
    currentShaderResourceGroup_ = {};
    currentShaderProgramReflection_ = nullptr;

    // Clear shadep parameters
    constantBuffers_.collection_.ClearAndInitialize(graphics_->GetCaps().constantBufferOffsetAlignment_);
    constantBuffers_.currentData_ = nullptr;
    constantBuffers_.currentHashes_.fill(0);

    currentDrawCommand_.constantBuffers_.fill({});

    // Clear arrays and draw commands
    shaderResources_.clear();
    drawCommands_.clear();
    scissorRects_.clear();
    scissorRects_.push_back(IntRect::ZERO);
}

void DrawCommandQueue::Execute()
{
    if (drawCommands_.empty())
        return;

    RenderContext* renderContext = graphics_->GetRenderContext();
    Diligent::IDeviceContext* deviceContext = renderContext->GetHandle();

    const RenderBackend& backend = renderContext->GetRenderDevice()->GetBackend();
    const bool isBaseVertexAndInstanceSupported = !IsOpenGLESBackend(backend);

    // Constant buffers to store all shader parameters for queue
    ea::vector<Diligent::IBuffer*> uniformBuffers;
    const unsigned numUniformBuffers = constantBuffers_.collection_.GetNumBuffers();
    uniformBuffers.resize(numUniformBuffers);
    for (unsigned i = 0; i < numUniformBuffers; ++i)
    {
        const unsigned size = constantBuffers_.collection_.GetGPUBufferSize(i);
        ConstantBuffer* uniformBuffer = graphics_->GetOrCreateConstantBuffer(VS, i, size);
        uniformBuffer->Update(constantBuffers_.collection_.GetBufferData(i));
        uniformBuffers[i] = uniformBuffer->GetHandle();
    }

    // Cached current state
    PipelineState* currentPipelineState = nullptr;
    Diligent::IShaderResourceBinding* currentShaderResourceBinding = nullptr;
    ShaderProgramReflection* currentShaderReflection = nullptr;
    RawBuffer* currentIndexBuffer = nullptr;
    RawVertexBufferArray currentVertexBuffers{};
    ShaderResourceRange currentShaderResources;
    PrimitiveType currentPrimitiveType{};
    unsigned currentScissorRect = M_MAX_UNSIGNED;

    // Set common state
    const float blendFactors[] = {1.0f, 1.0f, 1.0f, 1.0f};
    deviceContext->SetBlendFactors(blendFactors);

    for (const DrawCommandDescription& cmd : drawCommands_)
    {
        if (cmd.baseVertexIndex_ != 0 && !isBaseVertexAndInstanceSupported)
        {
            URHO3D_LOGWARNING("Base vertex index is not supported by current graphics API");
            continue;
        }

        // Set pipeline state
        if (cmd.pipelineState_ != currentPipelineState)
        {
            // TODO(diligent): This is used for shader reloading. Make better?
            cmd.pipelineState_->Restore();

            // TODO(diligent): Revisit error checking. Use default pipeline?
            if (!cmd.pipelineState_->GetHandle())
                continue;

            // Skip this pipeline if something goes wrong.
            deviceContext->SetPipelineState(cmd.pipelineState_->GetHandle());
            deviceContext->SetStencilRef(cmd.pipelineState_->GetDesc().stencilReferenceValue_);

            currentPipelineState = cmd.pipelineState_;
            currentShaderResourceBinding = cmd.pipelineState_->GetShaderResourceBinding();
            currentShaderReflection = cmd.pipelineState_->GetReflection();
            currentPrimitiveType = currentPipelineState->GetDesc().primitiveType_;

            // Reset current shader resources because mapping can be different
            currentShaderResources = {};
        }

        // Set scissor
        if (cmd.scissorRect_ != currentScissorRect)
        {
            const IntRect& scissorRect = scissorRects_[cmd.scissorRect_];

            Diligent::Rect internalRect;
            internalRect.left = scissorRect.left_;
            internalRect.top = scissorRect.top_;
            internalRect.right = scissorRect.right_;
            internalRect.bottom = scissorRect.bottom_;

            deviceContext->SetScissorRects(1, &internalRect, 0, 0);
            currentScissorRect = cmd.scissorRect_;
        }

        // Set index buffer
        if (cmd.indexBuffer_ != currentIndexBuffer)
        {
            Diligent::IBuffer* indexBufferHandle = cmd.indexBuffer_ ? cmd.indexBuffer_->GetHandle() : nullptr;
            deviceContext->SetIndexBuffer(indexBufferHandle, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            currentIndexBuffer = cmd.indexBuffer_;
        }

        // Set vertex buffers
        if (cmd.vertexBuffers_ != currentVertexBuffers
            || (cmd.instanceCount_ != 0 && !isBaseVertexAndInstanceSupported))
        {
            ea::array<Diligent::IBuffer*, MaxVertexStreams> vertexBufferHandles{};
            ea::array<Diligent::Uint64, MaxVertexStreams> vertexBufferOffsets{};

            for (unsigned i = 0; i < MaxVertexStreams; ++i)
            {
                RawBuffer* vertexBuffer = cmd.vertexBuffers_[i];
                if (!vertexBuffer)
                    continue;

                vertexBuffer->Resolve();

                const bool needInstanceOffset =
                    !isBaseVertexAndInstanceSupported && vertexBuffer->GetFlags().Test(BufferFlag::PerInstanceData);

                vertexBufferHandles[i] = vertexBuffer->GetHandle();
                vertexBufferOffsets[i] = needInstanceOffset ? cmd.instanceStart_ * vertexBuffer->GetStride() : 0;
            }

            deviceContext->SetVertexBuffers(0, MaxVertexStreams, vertexBufferHandles.data(), vertexBufferOffsets.data(),
                Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_NONE);

            currentVertexBuffers = cmd.vertexBuffers_;
        }

        // Set resources
        for (unsigned i = cmd.shaderResources_.first; i < cmd.shaderResources_.second; ++i)
        {
            const ShaderResourceData& data = shaderResources_[i];
            Texture* texture = data.texture_;

            if (renderContext->IsBoundAsRenderTarget(texture))
                texture = texture->GetBackupTexture(); // TODO(diligent): We should have default backup texture!
            if (!texture)
                continue;
            if (texture->GetResolveDirty())
                texture->Resolve();
            if (texture->GetLevelsDirty())
                texture->GenerateLevels();

            data.variable_->Set(texture->GetHandles().srv_);
        }

        for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
        {
            const auto group = static_cast<ShaderParameterGroup>(i);
            const UniformBufferReflection* uniformBufferReflection = currentShaderReflection->GetUniformBuffer(group);
            if (!uniformBufferReflection)
                continue;

            Diligent::IBuffer* uniformBuffer = uniformBuffers[cmd.constantBuffers_[i].index_];
            for (Diligent::IShaderResourceVariable* variable : uniformBufferReflection->variables_)
            {
                variable->SetBufferRange(uniformBuffer, cmd.constantBuffers_[i].offset_, cmd.constantBuffers_[i].size_);
            }
        }

        deviceContext->CommitShaderResources(
            currentShaderResourceBinding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        if (currentIndexBuffer)
        {
            Diligent::DrawIndexedAttribs drawAttrs;
            drawAttrs.NumIndices = cmd.indexCount_;
            drawAttrs.NumInstances = ea::max(1u, cmd.instanceCount_);
            drawAttrs.FirstIndexLocation = cmd.indexStart_;
            drawAttrs.FirstInstanceLocation = isBaseVertexAndInstanceSupported ? cmd.instanceStart_ : 0;
            drawAttrs.BaseVertex = cmd.baseVertexIndex_;
            drawAttrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
            drawAttrs.IndexType = GetIndexType(currentIndexBuffer);

            deviceContext->DrawIndexed(drawAttrs);
        }
        else
        {
            Diligent::DrawAttribs drawAttrs;
            drawAttrs.NumVertices = cmd.indexCount_;
            drawAttrs.NumInstances = ea::max(1u, cmd.instanceCount_);
            drawAttrs.StartVertexLocation = cmd.indexStart_;
            drawAttrs.FirstInstanceLocation = isBaseVertexAndInstanceSupported ? cmd.instanceStart_ : 0;
            drawAttrs.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;

            deviceContext->Draw(drawAttrs);
        }
    }
}

}
