/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
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

#ifndef InspectorDOMDebuggerAgent_h
#define InspectorDOMDebuggerAgent_h


#include "core/CoreExport.h"
#include "core/inspector/InspectorBaseAgent.h"
#include "core/inspector/InspectorDOMAgent.h"
#include "platform/v8_inspector/public/V8EventListenerInfo.h"
#include "wtf/HashMap.h"
#include "wtf/PassOwnPtr.h"
#include "wtf/text/WTFString.h"

namespace blink {

class Element;
class Event;
class EventTarget;
class InspectorDOMAgent;
class Node;
class V8DebuggerAgent;
class V8RuntimeAgent;

namespace protocol {
class DictionaryValue;
}

class CORE_EXPORT InspectorDOMDebuggerAgent final
    : public InspectorBaseAgent<InspectorDOMDebuggerAgent, protocol::Frontend::DOMDebugger>
    , public protocol::Backend::DOMDebugger {
    WTF_MAKE_NONCOPYABLE(InspectorDOMDebuggerAgent);
public:
    static InspectorDOMDebuggerAgent* create(v8::Isolate*, InspectorDOMAgent*, V8RuntimeAgent*, V8DebuggerAgent*);

    static void eventListenersInfoForTarget(v8::Isolate*, v8::Local<v8::Value>, V8EventListenerInfoList& listeners);

    ~InspectorDOMDebuggerAgent() override;
    DECLARE_VIRTUAL_TRACE();

    // DOMDebugger API for InspectorFrontend
    void setDOMBreakpoint(ErrorString*, int nodeId, const String& type) override;
    void removeDOMBreakpoint(ErrorString*, int nodeId, const String& type) override;
    void setEventListenerBreakpoint(ErrorString*, const String& eventName, const Maybe<String>& targetName) override;
    void removeEventListenerBreakpoint(ErrorString*, const String& eventName, const Maybe<String>& targetName) override;
    void setInstrumentationBreakpoint(ErrorString*, const String& eventName) override;
    void removeInstrumentationBreakpoint(ErrorString*, const String& eventName) override;
    void setXHRBreakpoint(ErrorString*, const String& url) override;
    void removeXHRBreakpoint(ErrorString*, const String& url) override;
    void getEventListeners(ErrorString*, const String16& objectId, OwnPtr<protocol::Array<protocol::DOMDebugger::EventListener>>* listeners) override;

    // InspectorInstrumentation API
    void allowNativeBreakpoint(const String& breakpointName, bool sync);
    void willInsertDOMNode(Node* parent);
    void didInvalidateStyleAttr(Node*);
    void didInsertDOMNode(Node*);
    void willRemoveDOMNode(Node*);
    void didRemoveDOMNode(Node*);
    void willModifyDOMAttr(Element*, const AtomicString&, const AtomicString&);
    void willSendXMLHttpRequest(const String& url);
    void willHandleEvent(EventTarget*, Event*, EventListener*, bool useCapture);
    void didFireWebGLError(const String& errorName);
    void didFireWebGLWarning();
    void didFireWebGLErrorOrWarning(const String& message);

    void disable(ErrorString*) override;
    void restore() override;
    void didCommitLoadForLocalFrame(LocalFrame*) override;

private:
    InspectorDOMDebuggerAgent(v8::Isolate*, InspectorDOMAgent*, V8RuntimeAgent*, V8DebuggerAgent*);

    void pauseOnNativeEventIfNeeded(PassOwnPtr<protocol::DictionaryValue> eventData, bool synchronous);
    PassOwnPtr<protocol::DictionaryValue> preparePauseOnNativeEventData(const String& eventName, const String* targetName);

    protocol::DictionaryValue* eventListenerBreakpoints();
    protocol::DictionaryValue* xhrBreakpoints();

    void descriptionForDOMEvent(Node* target, int breakpointType, bool insertion, protocol::DictionaryValue* description);
    void updateSubtreeBreakpoints(Node*, uint32_t rootMask, bool set);
    bool hasBreakpoint(Node*, int type);
    void setBreakpoint(ErrorString*, const String& eventName, const String& targetName);
    void removeBreakpoint(ErrorString*, const String& eventName, const String& targetName);

    void didAddBreakpoint();
    void didRemoveBreakpoint();
    void setEnabled(bool);

    void eventListeners(v8::Local<v8::Context>, v8::Local<v8::Value>, const String16& objectGroup, protocol::Array<protocol::DOMDebugger::EventListener>* listenersArray);
    PassOwnPtr<protocol::DOMDebugger::EventListener> buildObjectForEventListener(v8::Local<v8::Context>, const V8EventListenerInfo&, const String16& objectGroupId);

    v8::Isolate* m_isolate;
    Member<InspectorDOMAgent> m_domAgent;
    V8RuntimeAgent* m_runtimeAgent;
    V8DebuggerAgent* m_debuggerAgent;
    HeapHashMap<Member<Node>, uint32_t> m_domBreakpoints;
};

} // namespace blink


#endif // !defined(InspectorDOMDebuggerAgent_h)
