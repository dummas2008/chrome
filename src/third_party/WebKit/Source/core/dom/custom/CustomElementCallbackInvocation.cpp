/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Google Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/dom/custom/CustomElementCallbackInvocation.h"

#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/custom/CustomElementScheduler.h"

namespace blink {

class AttachedDetachedInvocation final : public CustomElementCallbackInvocation {
public:
    AttachedDetachedInvocation(CustomElementLifecycleCallbacks*, CustomElementLifecycleCallbacks::CallbackType which);

private:
    void dispatch(Element*) override;

    CustomElementLifecycleCallbacks::CallbackType m_which;
};

AttachedDetachedInvocation::AttachedDetachedInvocation(CustomElementLifecycleCallbacks* callbacks, CustomElementLifecycleCallbacks::CallbackType which)
    : CustomElementCallbackInvocation(callbacks)
    , m_which(which)
{
    DCHECK(m_which == CustomElementLifecycleCallbacks::AttachedCallback || m_which == CustomElementLifecycleCallbacks::DetachedCallback);
}

void AttachedDetachedInvocation::dispatch(Element* element)
{
    switch (m_which) {
    case CustomElementLifecycleCallbacks::AttachedCallback:
        callbacks()->attached(element);
        break;
    case CustomElementLifecycleCallbacks::DetachedCallback:
        callbacks()->detached(element);
        break;
    default:
        ASSERT_NOT_REACHED();
    }
}

class AttributeChangedInvocation final : public CustomElementCallbackInvocation {
public:
    AttributeChangedInvocation(CustomElementLifecycleCallbacks*, const AtomicString& name, const AtomicString& oldValue, const AtomicString& newValue);

private:
    void dispatch(Element*) override;

    AtomicString m_name;
    AtomicString m_oldValue;
    AtomicString m_newValue;
};

AttributeChangedInvocation::AttributeChangedInvocation(CustomElementLifecycleCallbacks* callbacks, const AtomicString& name, const AtomicString& oldValue, const AtomicString& newValue)
    : CustomElementCallbackInvocation(callbacks)
    , m_name(name)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void AttributeChangedInvocation::dispatch(Element* element)
{
    callbacks()->attributeChanged(element, m_name, m_oldValue, m_newValue);
}

class CreatedInvocation final : public CustomElementCallbackInvocation {
public:
    explicit CreatedInvocation(CustomElementLifecycleCallbacks* callbacks)
        : CustomElementCallbackInvocation(callbacks)
    {
    }

private:
    void dispatch(Element*) override;
    bool isCreatedCallback() const override { return true; }
};

void CreatedInvocation::dispatch(Element* element)
{
    if (element->inShadowIncludingDocument() && element->document().domWindow())
        CustomElementScheduler::scheduleCallback(callbacks(), element, CustomElementLifecycleCallbacks::AttachedCallback);
    callbacks()->created(element);
}

CustomElementCallbackInvocation* CustomElementCallbackInvocation::createInvocation(CustomElementLifecycleCallbacks* callbacks, CustomElementLifecycleCallbacks::CallbackType which)
{
    switch (which) {
    case CustomElementLifecycleCallbacks::CreatedCallback:
        return new CreatedInvocation(callbacks);

    case CustomElementLifecycleCallbacks::AttachedCallback:
    case CustomElementLifecycleCallbacks::DetachedCallback:
        return new AttachedDetachedInvocation(callbacks, which);
    default:
        ASSERT_NOT_REACHED();
        return nullptr;
    }
}

CustomElementCallbackInvocation* CustomElementCallbackInvocation::createAttributeChangedInvocation(CustomElementLifecycleCallbacks* callbacks, const AtomicString& name, const AtomicString& oldValue, const AtomicString& newValue)
{
    return new AttributeChangedInvocation(callbacks, name, oldValue, newValue);
}

DEFINE_TRACE(CustomElementCallbackInvocation)
{
    visitor->trace(m_callbacks);
    CustomElementProcessingStep::trace(visitor);
}

} // namespace blink
