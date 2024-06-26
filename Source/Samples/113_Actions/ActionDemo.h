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

#pragma once

#include "Sample.h"
#include <Urho3D/Actions/BaseAction.h>

/// This first example, maintaining tradition, prints a "Hello World" message.
/// Furthermore it shows:
///     - Using the Sample / Application classes, which initialize the Urho3D engine and run the main loop
///     - Adding a Text element to the graphical user interface
///     - Subscribing to and handling of update events
class ActionDemo : public Sample
{
    URHO3D_OBJECT(ActionDemo, Sample);

    struct DemoElement
    {
        SharedPtr<UIElement> element_;
        SharedPtr<Actions::BaseAction> action_;
    };
public:
    /// Construct.
    explicit ActionDemo(Context* context);

    /// Setup after engine initialization and before running the main loop.
    void Start() override;

private:
    /// Construct a new Text instance, containing the 'Hello World' String, and add it to the UI root element.
    void CreateUI();
    /// Add element to UI to showcase an action.
    DemoElement& AddElement(const IntVector2& pos, const SharedPtr<Actions::BaseAction>& action);
    /// Subscribe to application-wide logic update events.
    void SubscribeToEvents();

    void HandleMouseClick(StringHash eventType, VariantMap& eventData);

    void Deactivate() override;
   
    /// Handle the logic update event.
    void Update(float timestep) override;

    ea::vector<DemoElement> markers_;
};
