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

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/Profiler.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Geometry.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/IndexBuffer.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/VertexBuffer.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/Input/FreeFlyController.h>

#include "DynamicGeometry.h"

#include <Urho3D/DebugNew.h>

DynamicGeometry::DynamicGeometry(Context* context)
    : Sample(context)
    , animate_(true)
    , time_(0.0f)
{
}

void DynamicGeometry::Start()
{
    // Execute base class startup
    Sample::Start();

    // Create the scene content
    CreateScene();

    // Create the UI content
    CreateInstructions();

    // Setup the viewport for displaying the scene
    SetupViewport();

    // Set the mouse mode to use in the sample
    SetMouseMode(MM_RELATIVE);
    SetMouseVisible(false);
}

void DynamicGeometry::CreateScene()
{
    auto* cache = GetSubsystem<ResourceCache>();

    scene_ = new Scene(context_);

    // Create the Octree component to the scene so that drawable objects can be rendered. Use default volume
    // (-1000, -1000, -1000) to (1000, 1000, 1000)
    scene_->CreateComponent<Octree>();

    // Create a Zone for ambient light & fog control
    Node* zoneNode = scene_->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetFogColor(Color(0.2f, 0.2f, 0.2f));
    zone->SetFogStart(200.0f);
    zone->SetFogEnd(300.0f);

    // Create a directional light
    Node* lightNode = scene_->CreateChild("DirectionalLight");
    lightNode->SetDirection(Vector3(-0.6f, -1.0f, -0.8f)); // The direction vector does not need to be normalized
    auto* light = lightNode->CreateComponent<Light>();
    light->SetLightType(LIGHT_DIRECTIONAL);
    light->SetColor(Color(0.4f, 1.0f, 0.4f));
    light->SetSpecularIntensity(1.5f);

    // Get the original model and its unmodified vertices, which are used as source data for the animation
    auto* originalModel = cache->GetResource<Model>("Models/Box.mdl");
    if (!originalModel)
    {
        URHO3D_LOGERROR("Model not found, cannot initialize example scene");
        return;
    }
    // Get the vertex buffer from the first geometry's first LOD level
    VertexBuffer* buffer = originalModel->GetGeometry(0, 0)->GetVertexBuffer(0);
    const auto* vertexData = (const unsigned char*)buffer->Lock(0, buffer->GetVertexCount());
    if (vertexData)
    {
        unsigned numVertices = buffer->GetVertexCount();
        unsigned vertexSize = buffer->GetVertexSize();
        // Copy the original vertex positions
        for (unsigned i = 0; i < numVertices; ++i)
        {
            const Vector3& src = *reinterpret_cast<const Vector3*>(vertexData + i * vertexSize);
            originalVertices_.push_back(src);
        }
        buffer->Unlock();

        // Detect duplicate vertices to allow seamless animation
        vertexDuplicates_.resize(originalVertices_.size());
        for (unsigned i = 0; i < originalVertices_.size(); ++i)
        {
            vertexDuplicates_[i] = i; // Assume not a duplicate
            for (unsigned j = 0; j < i; ++j)
            {
                if (originalVertices_[i].Equals(originalVertices_[j]))
                {
                    vertexDuplicates_[i] = j;
                    break;
                }
            }
        }
    }
    else
    {
        URHO3D_LOGERROR("Failed to lock the model vertex buffer to get original vertices");
        return;
    }

    // Create StaticModels in the scene. Clone the model for each so that we can modify the vertex data individually
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            Node* node = scene_->CreateChild("Object");
            node->SetPosition(Vector3(x * 2.0f, 0.0f, y * 2.0f));
            auto* object = node->CreateComponent<StaticModel>();
            SharedPtr<Model> cloneModel = originalModel->Clone();
            object->SetModel(cloneModel);
            // Store the cloned vertex buffer that we will modify when animating
            animatingBuffers_.push_back(
                SharedPtr<VertexBuffer>(cloneModel->GetGeometry(0, 0)->GetVertexBuffer(0)));
        }
    }

    // Finally create one model (pyramid shape) and a StaticModel to display it from scratch
    // Note: there are duplicated vertices to enable face normals. We will calculate normals programmatically
    {
        const unsigned numVertices = 18;

        float vertexData[] = {
            // Position             Normal
            0.0f, 0.5f, 0.0f,       0.0f, 0.0f, 0.0f,
            0.5f, -0.5f, 0.5f,      0.0f, 0.0f, 0.0f,
            0.5f, -0.5f, -0.5f,     0.0f, 0.0f, 0.0f,

            0.0f, 0.5f, 0.0f,       0.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, 0.5f,     0.0f, 0.0f, 0.0f,
            0.5f, -0.5f, 0.5f,      0.0f, 0.0f, 0.0f,

            0.0f, 0.5f, 0.0f,       0.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f,    0.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, 0.5f,     0.0f, 0.0f, 0.0f,

            0.0f, 0.5f, 0.0f,       0.0f, 0.0f, 0.0f,
            0.5f, -0.5f, -0.5f,     0.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f,    0.0f, 0.0f, 0.0f,

            0.5f, -0.5f, -0.5f,     0.0f, 0.0f, 0.0f,
            0.5f, -0.5f, 0.5f,      0.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, 0.5f,     0.0f, 0.0f, 0.0f,

            0.5f, -0.5f, -0.5f,     0.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, 0.5f,     0.0f, 0.0f, 0.0f,
            -0.5f, -0.5f, -0.5f,    0.0f, 0.0f, 0.0f
        };

        const unsigned short indexData[] = {
            0, 1, 2,
            3, 4, 5,
            6, 7, 8,
            9, 10, 11,
            12, 13, 14,
            15, 16, 17
        };

        // Calculate face normals now
        for (unsigned i = 0; i < numVertices; i += 3)
        {
            Vector3& v1 = *(reinterpret_cast<Vector3*>(&vertexData[6 * i]));
            Vector3& v2 = *(reinterpret_cast<Vector3*>(&vertexData[6 * (i + 1)]));
            Vector3& v3 = *(reinterpret_cast<Vector3*>(&vertexData[6 * (i + 2)]));
            Vector3& n1 = *(reinterpret_cast<Vector3*>(&vertexData[6 * i + 3]));
            Vector3& n2 = *(reinterpret_cast<Vector3*>(&vertexData[6 * (i + 1) + 3]));
            Vector3& n3 = *(reinterpret_cast<Vector3*>(&vertexData[6 * (i + 2) + 3]));

            Vector3 edge1 = v1 - v2;
            Vector3 edge2 = v1 - v3;
            n1 = n2 = n3 = edge1.CrossProduct(edge2).Normalized();
        }

        SharedPtr<Model> fromScratchModel(new Model(context_));
        SharedPtr<VertexBuffer> vb(new VertexBuffer(context_));
        SharedPtr<IndexBuffer> ib(new IndexBuffer(context_));
        SharedPtr<Geometry> geom(new Geometry(context_));

#ifdef URHO3D_DEBUG
        // Always name your GPU objects, its usefully when things goes wrong
        vb->SetDebugName("DynamicGeometry");
        ib->SetDebugName("DynamicGeometry");
#endif

        // Shadowed buffer needed for raycasts to work, and so that data can be automatically restored on device loss
        vb->SetShadowed(true);
        // We could use the "legacy" element bitmask to define elements for more compact code, but let's demonstrate
        // defining the vertex elements explicitly to allow any element types and order
        ea::vector<VertexElement> elements;
        elements.push_back(VertexElement(TYPE_VECTOR3, SEM_POSITION));
        elements.push_back(VertexElement(TYPE_VECTOR3, SEM_NORMAL));
        vb->SetSize(numVertices, elements);
        vb->SetData(vertexData);

        ib->SetShadowed(true);
        ib->SetSize(numVertices, false);
        ib->SetData(indexData);

        geom->SetVertexBuffer(0, vb);
        geom->SetIndexBuffer(ib);
        geom->SetDrawRange(TRIANGLE_LIST, 0, numVertices);

        fromScratchModel->SetNumGeometries(1);
        fromScratchModel->SetGeometry(0, 0, geom);
        fromScratchModel->SetBoundingBox(BoundingBox(Vector3(-0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, 0.5f)));

        // Though not necessary to render, the vertex & index buffers must be listed in the model so that it can be saved properly
        ea::vector<SharedPtr<VertexBuffer> > vertexBuffers;
        ea::vector<SharedPtr<IndexBuffer> > indexBuffers;
        vertexBuffers.push_back(vb);
        indexBuffers.push_back(ib);
        // Morph ranges could also be not defined. Here we simply define a zero range (no morphing) for the vertex buffer
        ea::vector<unsigned> morphRangeStarts;
        ea::vector<unsigned> morphRangeCounts;
        morphRangeStarts.push_back(0);
        morphRangeCounts.push_back(0);
        fromScratchModel->SetVertexBuffers(vertexBuffers, morphRangeStarts, morphRangeCounts);
        fromScratchModel->SetIndexBuffers(indexBuffers);

        Node* node = scene_->CreateChild("FromScratchObject");
        node->SetPosition(Vector3(0.0f, 3.0f, 0.0f));
        auto* object = node->CreateComponent<StaticModel>();
        object->SetModel(fromScratchModel);
    }

    // Create the camera
    cameraNode_ = new Node(context_);
    cameraNode_->CreateComponent<FreeFlyController>();
    cameraNode_->SetPosition(Vector3(0.0f, 2.0f, -20.0f));
    auto* camera = cameraNode_->CreateComponent<Camera>();
    camera->SetFarClip(300.0f);
}

void DynamicGeometry::CreateInstructions()
{
    auto* cache = GetSubsystem<ResourceCache>();
    auto* ui = GetSubsystem<UI>();

    // Construct new Text object, set string to display and font to use
    auto* instructionText = GetUIRoot()->CreateChild<Text>();
    instructionText->SetText(
        "Use WASD keys and mouse/touch to move\n"
        "Space to toggle animation"
    );
    instructionText->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);
    // The text has multiple rows. Center them in relation to each other
    instructionText->SetTextAlignment(HA_CENTER);

    // Position the text relative to the screen center
    instructionText->SetHorizontalAlignment(HA_CENTER);
    instructionText->SetVerticalAlignment(VA_CENTER);
    instructionText->SetPosition(0, GetUIRoot()->GetHeight() / 4);
}

void DynamicGeometry::SetupViewport()
{
    auto* renderer = GetSubsystem<Renderer>();

    // Set up a viewport to the Renderer subsystem so that the 3D scene can be seen
    SharedPtr<Viewport> viewport(new Viewport(context_, scene_, cameraNode_->GetComponent<Camera>()));
    renderer->SetViewport(0, viewport);
}


void DynamicGeometry::AnimateObjects(float timeStep)
{
    URHO3D_PROFILE("AnimateObjects");

    time_ += timeStep * 100.0f;

    // Repeat for each of the cloned vertex buffers
    for (unsigned i = 0; i < animatingBuffers_.size(); ++i)
    {
        float startPhase = time_ + i * 30.0f;
        VertexBuffer* buffer = animatingBuffers_[i];

        // Lock the vertex buffer for update and rewrite positions with sine wave modulated ones
        // Cannot use discard lock as there is other data (normals, UVs) that we are not overwriting
        auto* vertexData = (unsigned char*)buffer->Lock(0, buffer->GetVertexCount());
        if (vertexData)
        {
            unsigned vertexSize = buffer->GetVertexSize();
            unsigned numVertices = buffer->GetVertexCount();
            for (unsigned j = 0; j < numVertices; ++j)
            {
                // If there are duplicate vertices, animate them in phase of the original
                float phase = startPhase + vertexDuplicates_[j] * 10.0f;
                Vector3& src = originalVertices_[j];
                Vector3& dest = *reinterpret_cast<Vector3*>(vertexData + j * vertexSize);
                dest.x_ = src.x_ * (1.0f + 0.1f * Sin(phase));
                dest.y_ = src.y_ * (1.0f + 0.1f * Sin(phase + 60.0f));
                dest.z_ = src.z_ * (1.0f + 0.1f * Sin(phase + 120.0f));
            }

            buffer->Unlock();
        }
    }
}

void DynamicGeometry::Update(float timeStep)
{
    // Toggle animation with space
    auto* input = GetSubsystem<Input>();

    if (input->GetKeyPress(KEY_SPACE))
        animate_ = !animate_;

    // Animate objects' vertex data if enabled
    if (animate_)
        AnimateObjects(timeStep);
}
