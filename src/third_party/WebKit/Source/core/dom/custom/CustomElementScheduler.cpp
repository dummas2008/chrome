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

#include "core/dom/custom/CustomElementScheduler.h"

#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/custom/CustomElementCallbackInvocation.h"
#include "core/dom/custom/CustomElementLifecycleCallbacks.h"
#include "core/dom/custom/CustomElementMicrotaskDispatcher.h"
#include "core/dom/custom/CustomElementMicrotaskImportStep.h"
#include "core/dom/custom/CustomElementMicrotaskResolutionStep.h"
#include "core/dom/custom/CustomElementMicrotaskRunQueue.h"
#include "core/dom/custom/CustomElementProcessingStack.h"
#include "core/dom/custom/CustomElementRegistrationContext.h"
#include "core/dom/custom/CustomElementSyncMicrotaskQueue.h"
#include "core/html/imports/HTMLImportChild.h"
#include "core/html/imports/HTMLImportsController.h"

namespace blink {

// FIXME: Consider moving the element's callback queue to ElementRareData.
typedef HeapHashMap<Member<Element>, Member<CustomElementCallbackQueue>> ElementCallbackQueueMap;

static ElementCallbackQueueMap& callbackQueues()
{
    DEFINE_STATIC_LOCAL(ElementCallbackQueueMap, map, (new ElementCallbackQueueMap));
    return map;
}

static CustomElementCallbackQueue& ensureCallbackQueue(Element* element)
{
    ElementCallbackQueueMap::ValueType* it = callbackQueues().add(element, nullptr).storedValue;
    if (!it->value)
        it->value = CustomElementCallbackQueue::create(element);
    return *it->value.get();
}

// Finds or creates the callback queue for element.
static CustomElementCallbackQueue& scheduleCallbackQueue(Element* element)
{
    CustomElementCallbackQueue& callbackQueue = ensureCallbackQueue(element);
    if (callbackQueue.inCreatedCallback()) {
        // Don't move it. Authors use the createdCallback like a
        // constructor. By not moving it, the createdCallback
        // completes before any other callbacks are entered for this
        // element.
        return callbackQueue;
    }

    if (CustomElementProcessingStack::inCallbackDeliveryScope()) {
        // The processing stack is active.
        CustomElementProcessingStack::instance().enqueue(&callbackQueue);
        return callbackQueue;
    }

    CustomElementMicrotaskDispatcher::instance().enqueue(&callbackQueue);
    return callbackQueue;
}

void CustomElementScheduler::scheduleCallback(CustomElementLifecycleCallbacks* callbacks, Element* element, CustomElementLifecycleCallbacks::CallbackType type)
{
    DCHECK(type != CustomElementLifecycleCallbacks::AttributeChangedCallback);

    if (!callbacks->hasCallback(type))
        return;

    CustomElementCallbackQueue& queue = scheduleCallbackQueue(element);
    queue.append(CustomElementCallbackInvocation::createInvocation(callbacks, type));
}

void CustomElementScheduler::scheduleAttributeChangedCallback(CustomElementLifecycleCallbacks* callbacks, Element* element, const AtomicString& name, const AtomicString& oldValue, const AtomicString& newValue)
{
    if (!callbacks->hasCallback(CustomElementLifecycleCallbacks::AttributeChangedCallback))
        return;

    CustomElementCallbackQueue& queue = scheduleCallbackQueue(element);
    queue.append(CustomElementCallbackInvocation::createAttributeChangedInvocation(callbacks, name, oldValue, newValue));
}

void CustomElementScheduler::resolveOrScheduleResolution(CustomElementRegistrationContext* context, Element* element, const CustomElementDescriptor& descriptor)
{
    if (CustomElementProcessingStack::inCallbackDeliveryScope()) {
        context->resolve(element, descriptor);
        return;
    }

    Document& document = element->document();
    CustomElementMicrotaskResolutionStep* step = CustomElementMicrotaskResolutionStep::create(context, element, descriptor);
    enqueueMicrotaskStep(document, step);
}

CustomElementMicrotaskImportStep* CustomElementScheduler::scheduleImport(HTMLImportChild* import)
{
    DCHECK(!import->hasFinishedLoading());
    DCHECK(import->parent());

    // Ownership of the new step is transferred to the parent
    // processing step, or the base queue.
    CustomElementMicrotaskImportStep* step = CustomElementMicrotaskImportStep::create(import);
    CustomElementMicrotaskImportStep* rawStep = step;
    enqueueMicrotaskStep(*(import->parent()->document()), step, import->isSync());
    return rawStep;
}

void CustomElementScheduler::enqueueMicrotaskStep(Document& document, CustomElementMicrotaskStep* step, bool importIsSync)
{
    Document& master = document.importsController() ? *(document.importsController()->master()) : document;
    master.customElementMicrotaskRunQueue()->enqueue(document.importLoader(), step, importIsSync);
}


void CustomElementScheduler::callbackDispatcherDidFinish()
{
    if (CustomElementMicrotaskDispatcher::instance().elementQueueIsEmpty())
        callbackQueues().clear();
}

void CustomElementScheduler::microtaskDispatcherDidFinish()
{
    DCHECK(!CustomElementProcessingStack::inCallbackDeliveryScope());
    callbackQueues().clear();
}

} // namespace blink
