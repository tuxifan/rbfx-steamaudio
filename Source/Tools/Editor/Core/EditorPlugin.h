//
// Copyright (c) 2017-2020 the rbfx project.
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

#include <Urho3D/Core/Object.h>

#include <EASTL/functional.h>

namespace Urho3D
{

/// Base class for any Editor plugin.
class EditorPlugin : public Object
{
    URHO3D_OBJECT(EditorPlugin, Object);

public:
    using Object::Object;

    virtual bool Apply(Object* target) = 0;
};

template <class T>
using EditorPluginFunction = void(*)(Context* context, T* target);

template <class T>
class EditorPluginT : public EditorPlugin
{
public:
    EditorPluginT(Context* context, EditorPluginFunction<T> function)
        : EditorPlugin(context)
        , function_(function)
    {
    }

    bool Apply(Object* target) final
    {
        if (auto derivedTarget = dynamic_cast<T*>(target))
        {
            function_(context_, derivedTarget);
            return true;
        }
        return false;
    }

private:
    EditorPluginFunction<T> function_;
};

}
