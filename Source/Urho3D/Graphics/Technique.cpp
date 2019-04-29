//
// Copyright (c) 2008-2019 the Urho3D project.
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
#include "../Core/ProcessUtils.h"
#include "../Core/Profiler.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Technique.h"
#include "../Graphics/Material.h"
#include "../Graphics/ShaderVariation.h"
#include "../IO/Log.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/XMLFile.h"

#include "../DebugNew.h"

namespace Urho3D
{

Pass::Pass(const ea::string& name) :
    blendMode_(BLEND_REPLACE),
    cullMode_(MAX_CULLMODES),
    depthTestMode_(CMP_LESSEQUAL),
    lightingMode_(LIGHTING_UNLIT),
    shadersLoadedFrameNumber_(0),
    alphaToCoverage_(false),
    depthWrite_(true),
    isDesktop_(false)
{
    name_ = name.to_lower();
    index_ = Technique::GetPassIndex(name_);

    // Guess default lighting mode from pass name
    if (index_ == Technique::basePassIndex || index_ == Technique::alphaPassIndex || index_ == Technique::materialPassIndex ||
        index_ == Technique::deferredPassIndex)
        lightingMode_ = LIGHTING_PERVERTEX;
    else if (index_ == Technique::lightPassIndex || index_ == Technique::litBasePassIndex || index_ == Technique::litAlphaPassIndex)
        lightingMode_ = LIGHTING_PERPIXEL;
}

Pass::~Pass() = default;

void Pass::SetBlendMode(BlendMode mode)
{
    blendMode_ = mode;
}

void Pass::SetCullMode(CullMode mode)
{
    cullMode_ = mode;
}

void Pass::SetDepthTestMode(CompareMode mode)
{
    depthTestMode_ = mode;
}

void Pass::SetLightingMode(PassLightingMode mode)
{
    lightingMode_ = mode;
}

void Pass::SetDepthWrite(bool enable)
{
    depthWrite_ = enable;
}

void Pass::SetAlphaToCoverage(bool enable)
{
    alphaToCoverage_ = enable;
}


void Pass::SetIsDesktop(bool enable)
{
    isDesktop_ = enable;
}

void Pass::SetVertexShader(const ea::string& name)
{
    vertexShaderName_ = name;
    ReleaseShaders();
}

void Pass::SetPixelShader(const ea::string& name)
{
    pixelShaderName_ = name;
    ReleaseShaders();
}

void Pass::SetVertexShaderDefines(const ea::string& defines)
{
    vertexShaderDefines_ = defines;
    ReleaseShaders();
}

void Pass::SetPixelShaderDefines(const ea::string& defines)
{
    pixelShaderDefines_ = defines;
    ReleaseShaders();
}

void Pass::SetVertexShaderDefineExcludes(const ea::string& excludes)
{
    vertexShaderDefineExcludes_ = excludes;
    ReleaseShaders();
}

void Pass::SetPixelShaderDefineExcludes(const ea::string& excludes)
{
    pixelShaderDefineExcludes_ = excludes;
    ReleaseShaders();
}

void Pass::ReleaseShaders()
{
    vertexShaders_.clear();
    pixelShaders_.clear();
    extraVertexShaders_.clear();
    extraPixelShaders_.clear();
}

void Pass::MarkShadersLoaded(unsigned frameNumber)
{
    shadersLoadedFrameNumber_ = frameNumber;
}

ea::string Pass::GetEffectiveVertexShaderDefines() const
{
    // Prefer to return just the original defines if possible
    if (vertexShaderDefineExcludes_.empty())
        return vertexShaderDefines_;

    ea::vector<ea::string> vsDefines = vertexShaderDefines_.split(' ');
    ea::vector<ea::string> vsExcludes = vertexShaderDefineExcludes_.split(' ');
    for (unsigned i = 0; i < vsExcludes.size(); ++i)
        vsDefines.erase_first(vsExcludes[i]);

    return ea::string::joined(vsDefines, " ");
}

ea::string Pass::GetEffectivePixelShaderDefines() const
{
    // Prefer to return just the original defines if possible
    if (pixelShaderDefineExcludes_.empty())
        return pixelShaderDefines_;

    ea::vector<ea::string> psDefines = pixelShaderDefines_.split(' ');
    ea::vector<ea::string> psExcludes = pixelShaderDefineExcludes_.split(' ');
    for (unsigned i = 0; i < psExcludes.size(); ++i)
        psDefines.erase_first(psExcludes[i]);

    return ea::string::joined(psDefines, " ");
}

ea::vector<ea::shared_ptr<ShaderVariation> >& Pass::GetVertexShaders(const StringHash& extraDefinesHash)
{
    // If empty hash, return the base shaders
    if (!extraDefinesHash.Value())
        return vertexShaders_;
    else
        return extraVertexShaders_[extraDefinesHash];
}

ea::vector<ea::shared_ptr<ShaderVariation> >& Pass::GetPixelShaders(const StringHash& extraDefinesHash)
{
    if (!extraDefinesHash.Value())
        return pixelShaders_;
    else
        return extraPixelShaders_[extraDefinesHash];
}

unsigned Technique::basePassIndex = 0;
unsigned Technique::alphaPassIndex = 0;
unsigned Technique::materialPassIndex = 0;
unsigned Technique::deferredPassIndex = 0;
unsigned Technique::lightPassIndex = 0;
unsigned Technique::litBasePassIndex = 0;
unsigned Technique::litAlphaPassIndex = 0;
unsigned Technique::shadowPassIndex = 0;

ea::unordered_map<ea::string, unsigned> Technique::passIndices;

Technique::Technique(Context* context) :
    Resource(context),
    isDesktop_(false)
{
#ifdef DESKTOP_GRAPHICS
    desktopSupport_ = true;
#else
    desktopSupport_ = false;
#endif
}

Technique::~Technique() = default;

void Technique::RegisterObject(Context* context)
{
    context->RegisterFactory<Technique>();
}

bool Technique::BeginLoad(Deserializer& source)
{
    passes_.clear();
    cloneTechniques_.clear();

    SetMemoryUse(sizeof(Technique));

    ea::shared_ptr<XMLFile> xml(context_->CreateObject<XMLFile>());
    if (!xml->Load(source))
        return false;

    XMLElement rootElem = xml->GetRoot();
    if (rootElem.HasAttribute("desktop"))
        isDesktop_ = rootElem.GetBool("desktop");

    ea::string globalVS = rootElem.GetAttribute("vs");
    ea::string globalPS = rootElem.GetAttribute("ps");
    ea::string globalVSDefines = rootElem.GetAttribute("vsdefines");
    ea::string globalPSDefines = rootElem.GetAttribute("psdefines");
    // End with space so that the pass-specific defines can be appended
    if (!globalVSDefines.empty())
        globalVSDefines += ' ';
    if (!globalPSDefines.empty())
        globalPSDefines += ' ';

    XMLElement passElem = rootElem.GetChild("pass");
    while (passElem)
    {
        if (passElem.HasAttribute("name"))
        {
            Pass* newPass = CreatePass(passElem.GetAttribute("name"));

            if (passElem.HasAttribute("desktop"))
                newPass->SetIsDesktop(passElem.GetBool("desktop"));

            // Append global defines only when pass does not redefine the shader
            if (passElem.HasAttribute("vs"))
            {
                newPass->SetVertexShader(passElem.GetAttribute("vs"));
                newPass->SetVertexShaderDefines(passElem.GetAttribute("vsdefines"));
            }
            else
            {
                newPass->SetVertexShader(globalVS);
                newPass->SetVertexShaderDefines(globalVSDefines + passElem.GetAttribute("vsdefines"));
            }
            if (passElem.HasAttribute("ps"))
            {
                newPass->SetPixelShader(passElem.GetAttribute("ps"));
                newPass->SetPixelShaderDefines(passElem.GetAttribute("psdefines"));
            }
            else
            {
                newPass->SetPixelShader(globalPS);
                newPass->SetPixelShaderDefines(globalPSDefines + passElem.GetAttribute("psdefines"));
            }

            newPass->SetVertexShaderDefineExcludes(passElem.GetAttribute("vsexcludes"));
            newPass->SetPixelShaderDefineExcludes(passElem.GetAttribute("psexcludes"));

            if (passElem.HasAttribute("lighting"))
            {
                ea::string lighting = passElem.GetAttributeLower("lighting");
                newPass->SetLightingMode((PassLightingMode)GetStringListIndex(lighting.c_str(), lightingModeNames,
                    LIGHTING_UNLIT));
            }

            if (passElem.HasAttribute("blend"))
            {
                ea::string blend = passElem.GetAttributeLower("blend");
                newPass->SetBlendMode((BlendMode)GetStringListIndex(blend.c_str(), blendModeNames, BLEND_REPLACE));
            }

            if (passElem.HasAttribute("cull"))
            {
                ea::string cull = passElem.GetAttributeLower("cull");
                newPass->SetCullMode((CullMode)GetStringListIndex(cull.c_str(), cullModeNames, MAX_CULLMODES));
            }

            if (passElem.HasAttribute("depthtest"))
            {
                ea::string depthTest = passElem.GetAttributeLower("depthtest");
                if (depthTest == "false")
                    newPass->SetDepthTestMode(CMP_ALWAYS);
                else
                    newPass->SetDepthTestMode((CompareMode)GetStringListIndex(depthTest.c_str(), compareModeNames, CMP_LESS));
            }

            if (passElem.HasAttribute("depthwrite"))
                newPass->SetDepthWrite(passElem.GetBool("depthwrite"));

            if (passElem.HasAttribute("alphatocoverage"))
                newPass->SetAlphaToCoverage(passElem.GetBool("alphatocoverage"));
        }
        else
            URHO3D_LOGERROR("Missing pass name");

        passElem = passElem.GetNext("pass");
    }

    return true;
}

void Technique::SetIsDesktop(bool enable)
{
    isDesktop_ = enable;
}

void Technique::ReleaseShaders()
{
    for (auto i = passes_.begin(); i != passes_.end(); ++i)
    {
        Pass* pass = i->get();
        if (pass)
            pass->ReleaseShaders();
    }
}

ea::shared_ptr<Technique> Technique::Clone(const ea::string& cloneName) const
{
    ea::shared_ptr<Technique> ret(context_->CreateObject<Technique>());
    ret->SetIsDesktop(isDesktop_);
    ret->SetName(cloneName);

    // Deep copy passes
    for (auto i = passes_.begin(); i != passes_.end(); ++i)
    {
        Pass* srcPass = i->get();
        if (!srcPass)
            continue;

        Pass* newPass = ret->CreatePass(srcPass->GetName());
        newPass->SetBlendMode(srcPass->GetBlendMode());
        newPass->SetDepthTestMode(srcPass->GetDepthTestMode());
        newPass->SetLightingMode(srcPass->GetLightingMode());
        newPass->SetDepthWrite(srcPass->GetDepthWrite());
        newPass->SetAlphaToCoverage(srcPass->GetAlphaToCoverage());
        newPass->SetIsDesktop(srcPass->IsDesktop());
        newPass->SetVertexShader(srcPass->GetVertexShader());
        newPass->SetPixelShader(srcPass->GetPixelShader());
        newPass->SetVertexShaderDefines(srcPass->GetVertexShaderDefines());
        newPass->SetPixelShaderDefines(srcPass->GetPixelShaderDefines());
        newPass->SetVertexShaderDefineExcludes(srcPass->GetVertexShaderDefineExcludes());
        newPass->SetPixelShaderDefineExcludes(srcPass->GetPixelShaderDefineExcludes());
    }

    return ret;
}

Pass* Technique::CreatePass(const ea::string& name)
{
    Pass* oldPass = GetPass(name);
    if (oldPass)
        return oldPass;

    ea::shared_ptr<Pass> newPass(new Pass(name));
    unsigned passIndex = newPass->GetIndex();
    if (passIndex >= passes_.size())
        passes_.resize(passIndex + 1);
    passes_[passIndex] = newPass;

    // Calculate memory use now
    SetMemoryUse((unsigned)(sizeof(Technique) + GetNumPasses() * sizeof(Pass)));

    return newPass;
}

void Technique::RemovePass(const ea::string& name)
{
    auto i = passIndices.find(name.to_lower());
    if (i == passIndices.end())
        return;
    else if (i->second < passes_.size() && passes_[i->second].get())
    {
        passes_[i->second].reset();
        SetMemoryUse((unsigned)(sizeof(Technique) + GetNumPasses() * sizeof(Pass)));
    }
}

bool Technique::HasPass(const ea::string& name) const
{
    auto i = passIndices.find(name.to_lower());
    return i != passIndices.end() ? HasPass(i->second) : false;
}

Pass* Technique::GetPass(const ea::string& name) const
{
    auto i = passIndices.find(name.to_lower());
    return i != passIndices.end() ? GetPass(i->second) : nullptr;
}

Pass* Technique::GetSupportedPass(const ea::string& name) const
{
    auto i = passIndices.find(name.to_lower());
    return i != passIndices.end() ? GetSupportedPass(i->second) : nullptr;
}

unsigned Technique::GetNumPasses() const
{
    unsigned ret = 0;

    for (auto i = passes_.begin(); i != passes_.end(); ++i)
    {
        if (i->get())
            ++ret;
    }

    return ret;
}

ea::vector<ea::string> Technique::GetPassNames() const
{
    ea::vector<ea::string> ret;

    for (auto i = passes_.begin(); i != passes_.end(); ++i)
    {
        Pass* pass = i->get();
        if (pass)
            ret.push_back(pass->GetName());
    }

    return ret;
}

ea::vector<Pass*> Technique::GetPasses() const
{
    ea::vector<Pass*> ret;

    for (auto i = passes_.begin(); i != passes_.end(); ++i)
    {
        Pass* pass = i->get();
        if (pass)
            ret.push_back(pass);
    }

    return ret;
}

ea::shared_ptr<Technique> Technique::CloneWithDefines(const ea::string& vsDefines, const ea::string& psDefines)
{
    // Return self if no actual defines
    if (vsDefines.empty() && psDefines.empty())
        return ea::shared_ptr<Technique>(this);

    ea::pair<StringHash, StringHash> key = ea::make_pair(StringHash(vsDefines), StringHash(psDefines));

    // Return existing if possible
    auto i = cloneTechniques_.find(key);
    if (i != cloneTechniques_.end())
        return i->second;

    // Set same name as the original for the clones to ensure proper serialization of the material. This should not be a problem
    // since the clones are never stored to the resource cache
    i = cloneTechniques_.insert(ea::make_pair(key, Clone(GetName()))).first;

    for (auto j = i->second->passes_.begin(); j != i->second->passes_.end(); ++j)
    {
        Pass* pass = (*j);
        if (!pass)
            continue;

        if (!vsDefines.empty())
            pass->SetVertexShaderDefines(pass->GetVertexShaderDefines() + " " + vsDefines);
        if (!psDefines.empty())
            pass->SetPixelShaderDefines(pass->GetPixelShaderDefines() + " " + psDefines);
    }

    return i->second;
}

unsigned Technique::GetPassIndex(const ea::string& passName)
{
    // Initialize built-in pass indices on first call
    if (passIndices.empty())
    {
        basePassIndex = passIndices["base"] = 0;
        alphaPassIndex = passIndices["alpha"] = 1;
        materialPassIndex = passIndices["material"] = 2;
        deferredPassIndex = passIndices["deferred"] = 3;
        lightPassIndex = passIndices["light"] = 4;
        litBasePassIndex = passIndices["litbase"] = 5;
        litAlphaPassIndex = passIndices["litalpha"] = 6;
        shadowPassIndex = passIndices["shadow"] = 7;
    }

    ea::string nameLower = passName.to_lower();
    auto i = passIndices.find(nameLower);
    if (i != passIndices.end())
        return i->second;
    else
    {
        unsigned newPassIndex = passIndices.size();
        passIndices[nameLower] = newPassIndex;
        return newPassIndex;
    }
}

}
