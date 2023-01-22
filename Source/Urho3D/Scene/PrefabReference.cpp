//
// Copyright (c) 2022-2022 the rbfx project.
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

#include <Urho3D/Precompiled.h>

#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Resource/ResourceEvents.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/PrefabReader.h>
#include <Urho3D/Scene/PrefabReference.h>

namespace Urho3D
{

PrefabReference::PrefabReference(Context* context)
    : BaseClassName(context)
    , prefabRef_(PrefabReference::GetTypeStatic())
{
}

PrefabReference::~PrefabReference()
{
    RemoveInstance();
}

void PrefabReference::RegisterObject(Context* context)
{
    context->AddFactoryReflection<PrefabReference>(Category_Scene);

    URHO3D_ACTION_STATIC_LABEL("Inline", InlineConservative, "Convert prefab reference to nodes and components");
    URHO3D_ACTION_STATIC_LABEL("Inline+", InlineAggressive, "Same as Inline. Also converts all temporary objects to persistent");

    URHO3D_MIXED_ACCESSOR_ATTRIBUTE("Prefab", GetPrefabAttr, SetPrefabAttr, ResourceRef, ResourceRef(PrefabResource::GetTypeStatic()), AM_DEFAULT)
        .SetScopeHint(AttributeScopeHint::Node);
}

void PrefabReference::ApplyAttributes()
{
    if (prefabDirty_)
    {
        prefabDirty_ = false;
        CreateInstance(true);
    }
}

const ScenePrefab& PrefabReference::GetNodePrefab() const
{
    if (node_ && prefab_)
        return prefab_->GetNodePrefab();
    return ScenePrefab::Empty;
}

bool PrefabReference::IsInstanceMatching(const Node* node, const ScenePrefab& nodePrefab, bool temporaryOnly) const
{
    if (!AreComponentsMatching(node, nodePrefab.GetComponents(), temporaryOnly))
        return false;

    if (!AreChildrenMatching(node, nodePrefab.GetChildren(), temporaryOnly))
        return false;

    return true;
}

bool PrefabReference::AreComponentsMatching(
    const Node* node, const ea::vector<SerializablePrefab>& componentPrefabs, bool temporaryOnly) const
{
    unsigned index = 0;
    for (const Component* component : node->GetComponents())
    {
        // Ignore extras, they may have been spawned by other components
        if (index >= componentPrefabs.size())
            return true;

        if (temporaryOnly && !component->IsTemporary())
            continue;

        if (component->GetType() != componentPrefabs[index].GetTypeNameHash())
            return false;

        ++index;
    }

    return index >= componentPrefabs.size();
}

bool PrefabReference::AreChildrenMatching(const Node* node, const ea::vector<ScenePrefab>& childPrefabs, bool temporaryOnly) const
{
    unsigned index = 0;
    for (const Node* child : node->GetChildren())
    {
        // Ignore extras, they may have been spawned by other components
        if (index >= childPrefabs.size())
            return true;

        if (temporaryOnly && !child->IsTemporary())
            continue;

        if (!IsInstanceMatching(child, childPrefabs[index], false /* ignore temporary flag */))
            return false;

        ++index;
    }

    return index >= childPrefabs.size();
}

void PrefabReference::ExportInstance(Node* node, const ScenePrefab& nodePrefab, bool temporaryOnly) const
{
    ExportComponents(node, nodePrefab.GetComponents(), temporaryOnly);
    ExportChildren(node, nodePrefab.GetChildren(), temporaryOnly);
}

void PrefabReference::ExportComponents(
    Node* node, const ea::vector<SerializablePrefab>& componentPrefabs, bool temporaryOnly) const
{
    unsigned index = 0;
    for (Component* component : node->GetComponents())
    {
        // Ignore extras, they may have been spawned by other components
        if (index >= componentPrefabs.size())
            return;

        if (temporaryOnly && !component->IsTemporary())
            continue;

        componentPrefabs[index].Export(component, PrefabLoadFlag::KeepTemporaryState);

        ++index;
    }
}

void PrefabReference::ExportChildren(Node* node, const ea::vector<ScenePrefab>& childPrefabs, bool temporaryOnly) const
{
    unsigned index = 0;
    for (Node* child : node->GetChildren())
    {
        // Ignore extras, they may have been spawned by other components
        if (index >= childPrefabs.size())
            return;

        if (temporaryOnly && !child->IsTemporary())
            continue;

        ExportInstance(child, childPrefabs[index], false /* ignore temporary flag */);

        ++index;
    }
}

bool PrefabReference::TryCreateInplace()
{
    const ScenePrefab& nodePrefab = GetNodePrefab();

    if (nodePrefab.IsEmpty())
        return false;

    if (!IsInstanceMatching(node_, nodePrefab, true /* temporary only */))
        return false;

    ExportInstance(node_, nodePrefab, true /* temporary only */);
    return true;
}

void PrefabReference::RemoveTemporaryComponents(Node* node) const
{
    const auto& components = node->GetComponents();
    const int numComponents = static_cast<int>(node->GetNumComponents());

    for (int i = numComponents - 1; i >= 0; --i)
    {
        Component* component = components[i];
        if (component->IsTemporary())
        {
            if (component != this)
                node->RemoveComponent(component);
            else
            {
                URHO3D_LOGWARNING("PrefabReference component should not be temporary");
                component->SetTemporary(false);
            }
        }
    }
}

void PrefabReference::RemoveTemporaryChildren(Node* node) const
{
    const auto& children = node->GetChildren();
    const int numChildren = static_cast<int>(node->GetNumChildren());

    for (int i = numChildren - 1; i >= 0; --i)
    {
        Node* child = children[i];
        if (child->IsTemporary())
            node->RemoveChild(child);
    }
}

void PrefabReference::RemoveInstance()
{
    if (Node* instanceNode = instanceNode_)
    {
        RemoveTemporaryComponents(instanceNode);
        RemoveTemporaryChildren(instanceNode);
    }

    instanceNode_ = nullptr;
}

void PrefabReference::InstantiatePrefab(const ScenePrefab& nodePrefab)
{
    const auto flags = PrefabLoadFlag::KeepExistingComponents | PrefabLoadFlag::KeepExistingChildren
        | PrefabLoadFlag::LoadAsTemporary | PrefabLoadFlag::IgnoreRootAttributes;
    PrefabReaderFromMemory reader{nodePrefab};
    node_->Load(reader, flags);
}

void PrefabReference::CreateInstance(bool tryInplace)
{
    // Remove existing instance if moved to another node
    if (instanceNode_ && instanceNode_ != node_)
        RemoveInstance();

    // Cannot spawn instance without node
    if (!node_)
        return;

    const ScenePrefab& nodePrefab = GetNodePrefab();
    instanceNode_ = node_;
    numInstanceComponents_ = nodePrefab.GetComponents().size();
    numInstanceChildren_ = nodePrefab.GetChildren().size();

    // Try to create inplace first
    if (tryInplace && TryCreateInplace())
        return;

    RemoveTemporaryComponents(node_);
    RemoveTemporaryChildren(node_);
    InstantiatePrefab(nodePrefab);
}

void PrefabReference::SetPrefab(PrefabResource* prefab, bool createInstance)
{
    if (prefab == prefab_)
    {
        return;
    }

    if (prefab_)
    {
        UnsubscribeFromEvent(prefab_, E_RELOADFINISHED);
    }

    prefab_ = prefab;

    if (prefab_)
    {
        SubscribeToEvent(prefab_, E_RELOADFINISHED, [this](StringHash, VariantMap&) { CreateInstance(); });
        prefabRef_ = GetResourceRef(prefab_, PrefabResource::GetTypeStatic());
    }
    else
    {
        prefabRef_ = ResourceRef(PrefabResource::GetTypeStatic());
    }

    if (createInstance)
        CreateInstance();
}

/// Set reference to prefab resource.
void PrefabReference::SetPrefabAttr(ResourceRef prefab)
{
    if (prefab.name_.empty())
    {
        SetPrefab(nullptr, false);
    }
    else
    {
        auto cache = context_->GetSubsystem<ResourceCache>();
        SetPrefab(cache->GetResource<PrefabResource>(prefab.name_), false);
    }

    prefabRef_ = prefab;
    prefabDirty_ = true;
}

void PrefabReference::Inline(PrefabInlineFlags flags)
{
    if (!node_)
        return;

    // Hold self until the end of the function
    Node* node = node_;
    instanceNode_ = nullptr;

    SharedPtr<PrefabReference> self{this};
    Remove();

    // Some temporary components and children are spawned by the prefab itself.
    // Other temporary components and children may be spawned by the components in prefab.
    if (flags.Test(PrefabInlineFlag::KeepOtherTemporary))
    {
        const auto& components = node->GetComponents();
        const auto& children = node->GetChildren();

        for (unsigned index = 0; index < ea::min(numInstanceComponents_, components.size()); ++index)
            components[index]->SetTemporary(false);

        for (unsigned index = 0; index < ea::min(numInstanceChildren_, children.size()); ++index)
            children[index]->SetTemporary(false);
    }
    else
    {
        for (const auto& component : node->GetComponents())
            component->SetTemporary(false);
        for (const auto& child : node->GetChildren())
            child->SetTemporary(false);
    }
}

void PrefabReference::OnSetEnabled()
{
    if (!IsEnabledEffective())
        RemoveInstance();
    else if (instanceNode_ != node_)
        CreateInstance(true);
}

}
