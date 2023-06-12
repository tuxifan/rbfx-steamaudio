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

#include "../Core/Context.h"
#include "../IO/Log.h"
#include "../Graphics/Texture2D.h"
#include "../Resource/Image.h"
#include "../UI/Font.h"
#include "../UI/FontFace.h"

#include "../DebugNew.h"

namespace Urho3D
{

FontFace::FontFace(Font* font) :
    font_(font)
{
}

FontFace::~FontFace()
{
    if (font_)
    {
        // When a face is unloaded, deduct the used texture data size from the parent font
        unsigned totalTextureSize = 0;
        for (unsigned i = 0; i < textures_.size(); ++i)
            totalTextureSize += textures_[i]->GetWidth() * textures_[i]->GetHeight();
        font_->SetMemoryUse(font_->GetMemoryUse() - totalTextureSize);
    }
}

const FontGlyph* FontFace::GetGlyph(unsigned c)
{
    auto i = glyphMapping_.find(c);
    if (i != glyphMapping_.end())
    {
        FontGlyph& glyph = i->second;
        glyph.used_ = true;
        return &glyph;
    }
    else
        return nullptr;
}

float FontFace::GetKerning(unsigned c, unsigned d) const
{
    if (kerningMapping_.empty())
        return 0;

    if (c == '\n' || d == '\n')
        return 0;

    if (c > 0xffff || d > 0xffff)
        return 0;

    unsigned value = (c << 16u) + d;

    auto i = kerningMapping_.find(value);
    if (i != kerningMapping_.end())
        return i->second;

    return 0;
}

bool FontFace::IsDataLost() const
{
    for (unsigned i = 0; i < textures_.size(); ++i)
    {
        if (textures_[i]->IsDataLost())
            return true;
    }
    return false;
}


SharedPtr<Texture2D> FontFace::CreateFaceTexture()
{
    auto texture = MakeShared<Texture2D>(font_->GetContext());
    texture->SetMipsToSkip(QUALITY_LOW, 0); // No quality reduction
    texture->SetNumLevels(1); // No mipmaps
    texture->SetAddressMode(TextureCoordinate::U, ADDRESS_CLAMP);
    texture->SetAddressMode(TextureCoordinate::V, ADDRESS_CLAMP);
    return texture;
}

SharedPtr<Texture2D> FontFace::LoadFaceTexture(const SharedPtr<Image>& image)
{
    SharedPtr<Texture2D> texture = CreateFaceTexture();
    texture->SetName(Format("{}:{}", image->GetName(), pointSize_));
    if (!texture->SetData(image))
    {
        URHO3D_LOGERROR("Could not load texture from image resource");
        return SharedPtr<Texture2D>();
    }
    return texture;
}

}
