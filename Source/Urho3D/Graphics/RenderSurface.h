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

#pragma once

#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/Viewport.h"

#include <Diligent/Common/interface/RefCntAutoPtr.hpp>
#include <Diligent/Graphics/GraphicsEngine/interface/TextureView.h>

#include <atomic>

namespace Urho3D
{

class Texture;

/// %Color or depth-stencil surface that can be rendered into.
class URHO3D_API RenderSurface : public RefCounted
{
    friend class Texture2D;
    friend class Texture2DArray;
    friend class TextureCube;

public:
    /// Construct with parent texture.
    explicit RenderSurface(Texture* parentTexture);
    /// Destruct.
    ~RenderSurface() override;

    /// Set number of viewports.
    /// @property
    void SetNumViewports(unsigned num);
    /// Set viewport.
    /// @property{set_viewports}
    void SetViewport(unsigned index, Viewport* viewport);
    /// Set viewport update mode. Default is to update when visible.
    /// @property
    void SetUpdateMode(RenderSurfaceUpdateMode mode);
    /// Set linked color rendertarget.
    /// @property
    void SetLinkedRenderTarget(RenderSurface* renderTarget);
    /// Set linked depth-stencil surface.
    /// @property
    void SetLinkedDepthStencil(RenderSurface* depthStencil);
    /// Queue manual update of the viewport(s).
    void QueueUpdate();
    /// Release surface.
    void Release();
    /// Mark the GPU resource destroyed on graphics context destruction. Only used on OpenGL.
    void OnDeviceLost();
    /// Create renderbuffer that cannot be sampled as a texture. Only used on OpenGL.
    bool CreateRenderBuffer(unsigned width, unsigned height, unsigned format, int multiSample);

    /// Return width.
    /// @property
    int GetWidth() const;

    /// Return height.
    /// @property
    int GetHeight() const;

    /// Return size.
    IntVector2 GetSize() const;

    /// Return usage.
    /// @property
    TextureUsage GetUsage() const;

    /// Return multisampling level.
    int GetMultiSample() const;

    /// Return multisampling autoresolve mode.
    bool GetAutoResolve() const;

    /// Return number of viewports.
    /// @property
    unsigned GetNumViewports() const { return viewports_.size(); }

    /// Return viewport by index.
    /// @property{get_viewports}
    Viewport* GetViewport(unsigned index) const;

    /// Return viewport update mode.
    /// @property
    RenderSurfaceUpdateMode GetUpdateMode() const { return updateMode_; }

    /// Return linked color rendertarget.
    /// @property
    RenderSurface* GetLinkedRenderTarget() const { return linkedRenderTarget_; }

    /// Return linked depth-stencil surface.
    /// @property
    RenderSurface* GetLinkedDepthStencil() const { return linkedDepthStencil_; }

    /// Return whether manual update queued. Called internally.
    bool IsUpdateQueued() const { return updateQueued_.load(std::memory_order_relaxed); }

    /// Reset update queued flag. Called internally.
    void ResetUpdateQueued();

    /// Return parent texture.
    /// @property
    Texture* GetParentTexture() const { return parentTexture_; }

#ifdef URHO3D_DILIGENT
    /// Return Diligent rendertarget or depth-stencil view.
    Diligent::RefCntAutoPtr<Diligent::ITextureView> GetRenderTargetView() const { return renderTargetView_; }
    /// Return Diligent read-only depth-stencil view. May be null if not applicable.
    Diligent::RefCntAutoPtr<Diligent::ITextureView> GetReadOnlyView() const { return readOnlyView_; }
#else
    /// Return Direct3D11 rendertarget or depth-stencil view. Not valid on OpenGL.
    void* GetRenderTargetView() const { return renderTargetView_; }

    /// Return Direct3D11 read-only depth-stencil view. May be null if not applicable. Not valid on OpenGL.
    void* GetReadOnlyView() const { return readOnlyView_; }

    /// Return surface's OpenGL target.
    unsigned GetTarget() const { return target_; }

    /// Return OpenGL renderbuffer if created.
    unsigned GetRenderBuffer() const { return renderBuffer_; }
#endif
    /// Return whether multisampled rendertarget needs resolve.
    /// @property
    bool IsResolveDirty() const { return resolveDirty_; }

    /// Set or clear the need resolve flag. Called internally by Graphics.
    void SetResolveDirty(bool enable) { resolveDirty_ = enable; }

    /// Property getters that can work with null RenderSurface corresponding to main viewport
    /// @{
    static IntVector2 GetSize(Graphics* graphics, const RenderSurface* renderSurface);
    static IntRect GetRect(Graphics* graphics, const RenderSurface* renderSurface);
    static unsigned GetFormat(Graphics* graphics, const RenderSurface* renderSurface);
    static int GetMultiSample(Graphics* graphics, const RenderSurface* renderSurface);
    static bool GetSRGB(Graphics* graphics, const RenderSurface* renderSurface);
    /// @}

private:
    /// Graphics subsystem.
    WeakPtr<Graphics> graphics_;
    /// Parent texture.
    WeakPtr<Texture> parentTexture_;

#ifdef URHO3D_DILIGENT
    /// Diligent rendertarget or depth-stencil view.
    Diligent::RefCntAutoPtr<Diligent::ITextureView> renderTargetView_;
    /// Diligent read-only depth-stencil view. Present only on depth-stencil surfaces.
    Diligent::RefCntAutoPtr<Diligent::ITextureView> readOnlyView_;
#else
    // https://github.com/doxygen/doxygen/issues/7623
    union
    {
        /// Direct3D11 rendertarget or depth-stencil view.
        /// @nobind
        void* renderTargetView_;
        /// OpenGL renderbuffer name.
        /// @nobind
        unsigned renderBuffer_;
    };

    // https://github.com/doxygen/doxygen/issues/7623
    union
    {
        /// Direct3D11 read-only depth-stencil view. Present only on depth-stencil surfaces.
        /// @nobind
        void* readOnlyView_;
        /// OpenGL target.
        /// @nobind
        unsigned target_;
    };
#endif

    /// Viewports.
    ea::vector<SharedPtr<Viewport> > viewports_;
    /// Linked color buffer.
    WeakPtr<RenderSurface> linkedRenderTarget_;
    /// Linked depth buffer.
    WeakPtr<RenderSurface> linkedDepthStencil_;
    /// Update mode for viewports.
    RenderSurfaceUpdateMode updateMode_{SURFACE_UPDATEVISIBLE};
    /// Update queued flag.
    std::atomic_bool updateQueued_{ false };
    /// Multisampled resolve dirty flag.
    bool resolveDirty_{};
};

}
