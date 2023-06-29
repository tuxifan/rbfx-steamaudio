// Copyright (c) 2017-2020 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "Urho3D/Core/Object.h"
#include "Urho3D/Graphics/ConstantBuffer.h"
#include "Urho3D/Graphics/ConstantBufferCollection.h"
#include "Urho3D/IO/Log.h"
#include "Urho3D/RenderAPI/PipelineState.h"
#include "Urho3D/RenderAPI/ShaderProgramReflection.h"

namespace Urho3D
{

class Graphics;
class RawBuffer;
class Texture;

/// Reference to input shader resource. Only textures are supported now.
struct ShaderResourceDesc
{
    StringHash name_{};
    Texture* texture_{};
};

/// Generic description of shader parameter.
/// Beware of Variant allocations for types larger than Vector4!
struct ShaderParameterDesc
{
    StringHash name_;
    Variant value_;
};

/// Shader resource group, range in array.
using ShaderResourceRange = ea::pair<unsigned, unsigned>;

/// Description of draw command.
struct DrawCommandDescription
{
    PipelineState* pipelineState_{};
    RawVertexBufferArray vertexBuffers_{};
    RawBuffer* indexBuffer_{};

    ea::array<ConstantBufferCollectionRef, MAX_SHADER_PARAMETER_GROUPS> constantBuffers_;

    ShaderResourceRange shaderResources_;

    /// Index of scissor rectangle. 0 if disabled.
    unsigned scissorRect_{};

    /// Draw call parameters
    /// @{
    unsigned indexStart_{};
    unsigned indexCount_{};
    unsigned baseVertexIndex_{};
    unsigned instanceStart_{};
    unsigned instanceCount_{};
    /// @}
};

/// Queue of draw commands.
class DrawCommandQueue : public RefCounted
{
public:
    /// Construct.
    DrawCommandQueue(Graphics* graphics);

    /// Reset queue.
    void Reset();

    /// Set pipeline state. Must be called first.
    void SetPipelineState(PipelineState* pipelineState)
    {
        URHO3D_ASSERT(pipelineState);

        currentDrawCommand_.pipelineState_ = pipelineState;
        currentShaderProgramReflection_ = pipelineState->GetReflection();
    }

    /// Set scissor rect.
    void SetScissorRect(const IntRect& scissorRect)
    {
        if (scissorRects_.size() > 1 && scissorRects_.back() == scissorRect)
            return;

        currentDrawCommand_.scissorRect_ = scissorRects_.size();
        scissorRects_.push_back(scissorRect);
    }

    /// Begin shader parameter group. All parameters shall be set for each draw command.
    bool BeginShaderParameterGroup(ShaderParameterGroup group, bool differentFromPrevious = false)
    {
        const UniformBufferReflection* uniformBuffer = currentShaderProgramReflection_->GetUniformBuffer(group);
        if (!uniformBuffer)
        {
            // If contents changed, forget cached constant buffer
            if (differentFromPrevious)
                constantBuffers_.currentHashes_[group] = 0;
            return false;
        }

        // If data and/or layout changed, rebuild block
        if (differentFromPrevious || uniformBuffer->hash_ != constantBuffers_.currentHashes_[group])
        {
            const auto& refAndData = constantBuffers_.collection_.AddBlock(uniformBuffer->size_);

            currentDrawCommand_.constantBuffers_[group] = refAndData.first;
            constantBuffers_.currentData_ = refAndData.second;
            constantBuffers_.currentHashes_[group] = uniformBuffer->hash_;
            constantBuffers_.currentGroup_ = group;
            return true;
        }

        return false;
    }

    /// Add shader parameter. Shall be called only if BeginShaderParameterGroup returned true.
    template <class T>
    void AddShaderParameter(StringHash name, const T& value)
    {
        const auto* paramInfo = currentShaderProgramReflection_->GetUniform(name);
        if (paramInfo)
        {
            if (constantBuffers_.currentGroup_ != paramInfo->group_)
            {
                URHO3D_LOGERROR("Shader parameter #{} '{}' shall be stored in group {} instead of group {}",
                    name.Value(), name.Reverse(), constantBuffers_.currentGroup_, paramInfo->group_);
                return;
            }

            if (!ConstantBufferCollection::StoreParameter(constantBuffers_.currentData_ + paramInfo->offset_,
                paramInfo->size_, value))
            {
                URHO3D_LOGERROR("Shader parameter #{} '{}' has unexpected type, {} bytes expected",
                    name.Value(), name.Reverse(), paramInfo->size_);
            }
        }
    }

    /// Commit shader parameter group. Shall be called only if BeginShaderParameterGroup returned true.
    void CommitShaderParameterGroup(ShaderParameterGroup group)
    {
        // All data is already stored, nothing to do
        constantBuffers_.currentGroup_ = MAX_SHADER_PARAMETER_GROUPS;
    }

    /// Add shader resource.
    void AddShaderResource(StringHash name, Texture* texture)
    {
        const ShaderResourceReflection* shaderParameter = currentShaderProgramReflection_->GetShaderResource(name);
        if (!shaderParameter || !shaderParameter->variable_)
            return;

        shaderResources_.push_back(ShaderResourceData{shaderParameter->variable_, texture});
        ++currentShaderResourceGroup_.second;
    }

    /// Commit shader resources added since previous commit.
    void CommitShaderResources()
    {
        currentDrawCommand_.shaderResources_ = currentShaderResourceGroup_;
        currentShaderResourceGroup_.first = shaderResources_.size();
        currentShaderResourceGroup_.second = currentShaderResourceGroup_.first;
    }

    /// Set vertex buffers.
    void SetVertexBuffers(RawVertexBufferArray buffers)
    {
        currentDrawCommand_.vertexBuffers_ = buffers;
    }

    /// Set index buffer.
    void SetIndexBuffer(RawBuffer* buffer)
    {
        currentDrawCommand_.indexBuffer_ = buffer;
    }

    /// Enqueue draw non-indexed geometry.
    void Draw(unsigned vertexStart, unsigned vertexCount)
    {
        URHO3D_ASSERT(!currentDrawCommand_.indexBuffer_);

        currentDrawCommand_.indexStart_ = vertexStart;
        currentDrawCommand_.indexCount_ = vertexCount;
        currentDrawCommand_.baseVertexIndex_ = 0;
        currentDrawCommand_.instanceStart_ = 0;
        currentDrawCommand_.instanceCount_ = 0;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed geometry.
    void DrawIndexed(unsigned indexStart, unsigned indexCount)
    {
        URHO3D_ASSERT(currentDrawCommand_.indexBuffer_);

        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = 0;
        currentDrawCommand_.instanceStart_ = 0;
        currentDrawCommand_.instanceCount_ = 0;
        // TODO(diligent): Revisit this
        // fix: don't push command if index start and index count is 0
        if (indexStart == 0 && indexCount == 0)
            return;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed geometry with vertex index offset.
    void DrawIndexed(unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex)
    {
        URHO3D_ASSERT(currentDrawCommand_.indexBuffer_);

        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = baseVertexIndex;
        currentDrawCommand_.instanceStart_ = 0;
        currentDrawCommand_.instanceCount_ = 0;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed, instanced geometry.
    void DrawIndexedInstanced(unsigned indexStart, unsigned indexCount, unsigned instanceStart, unsigned instanceCount)
    {
        URHO3D_ASSERT(currentDrawCommand_.indexBuffer_);

        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = 0;
        currentDrawCommand_.instanceStart_ = instanceStart;
        currentDrawCommand_.instanceCount_ = instanceCount;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Enqueue draw indexed, instanced geometry with vertex index offset.
    void DrawIndexedInstanced(unsigned indexStart, unsigned indexCount, unsigned baseVertexIndex,
        unsigned instanceStart, unsigned instanceCount)
    {
        URHO3D_ASSERT(currentDrawCommand_.indexBuffer_);

        currentDrawCommand_.indexStart_ = indexStart;
        currentDrawCommand_.indexCount_ = indexCount;
        currentDrawCommand_.baseVertexIndex_ = baseVertexIndex;
        currentDrawCommand_.instanceStart_ = instanceStart;
        currentDrawCommand_.instanceCount_ = instanceCount;
        drawCommands_.push_back(currentDrawCommand_);
    }

    /// Execute commands in the queue.
    void Execute();

private:
    /// Cached pointer to Graphics.
    Graphics* graphics_{};

    /// Shader parameters data when constant buffers are used.
    struct ConstantBuffersData
    {
        /// Constant buffers.
        ConstantBufferCollection collection_;

        /// Current constant buffer group.
        ShaderParameterGroup currentGroup_{ MAX_SHADER_PARAMETER_GROUPS };
        /// Current pointer to constant buffer data.
        unsigned char* currentData_{};
        /// Current constant buffer layout hashes.
        ea::array<unsigned, MAX_SHADER_PARAMETER_GROUPS> currentHashes_{};
    } constantBuffers_;

    struct ShaderResourceData
    {
        Diligent::IShaderResourceVariable* variable_{};
        Texture* texture_{};
    };

    /// Shader resources.
    ea::vector<ShaderResourceData> shaderResources_;
    /// Scissor rects.
    ea::vector<IntRect> scissorRects_;
    /// Draw operations.
    ea::vector<DrawCommandDescription> drawCommands_;

    /// Current draw operation.
    DrawCommandDescription currentDrawCommand_;
    /// Current shader resource group.
    ShaderResourceRange currentShaderResourceGroup_;
    ShaderProgramReflection* currentShaderProgramReflection_{};

};

}
