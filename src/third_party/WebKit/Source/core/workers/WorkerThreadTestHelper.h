// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bindings/core/v8/ScriptCallStack.h"
#include "bindings/core/v8/V8CacheOptions.h"
#include "bindings/core/v8/V8GCController.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/workers/WorkerClients.h"
#include "core/workers/WorkerLoaderProxy.h"
#include "core/workers/WorkerReportingProxy.h"
#include "core/workers/WorkerThread.h"
#include "core/workers/WorkerThreadStartupData.h"
#include "platform/ThreadSafeFunctional.h"
#include "platform/WaitableEvent.h"
#include "platform/WebThreadSupportingGC.h"
#include "platform/heap/Handle.h"
#include "platform/network/ContentSecurityPolicyParsers.h"
#include "platform/weborigin/KURL.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "public/platform/WebAddressSpace.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "wtf/CurrentTime.h"
#include "wtf/Forward.h"
#include "wtf/OwnPtr.h"
#include "wtf/PassOwnPtr.h"
#include "wtf/Vector.h"
#include <v8.h>

namespace blink {

class MockWorkerLoaderProxyProvider : public WorkerLoaderProxyProvider {
public:
    MockWorkerLoaderProxyProvider() { }
    ~MockWorkerLoaderProxyProvider() override { }

    void postTaskToLoader(PassOwnPtr<ExecutionContextTask>) override
    {
        NOTIMPLEMENTED();
    }

    bool postTaskToWorkerGlobalScope(PassOwnPtr<ExecutionContextTask>) override
    {
        NOTIMPLEMENTED();
        return false;
    }
};

class MockWorkerReportingProxy : public WorkerReportingProxy {
public:
    MockWorkerReportingProxy() { }
    ~MockWorkerReportingProxy() override { }

    MOCK_METHOD5(reportException, void(const String& errorMessage, int lineNumber, int columnNumber, const String& sourceURL, int exceptionId));
    MOCK_METHOD1(reportConsoleMessage, void(ConsoleMessage*));
    MOCK_METHOD1(postMessageToPageInspector, void(const String&));
    MOCK_METHOD0(postWorkerConsoleAgentEnabled, void());
    MOCK_METHOD1(didEvaluateWorkerScript, void(bool success));
    MOCK_METHOD1(workerGlobalScopeStarted, void(WorkerGlobalScope*));
    MOCK_METHOD0(workerGlobalScopeClosed, void());
    MOCK_METHOD0(workerThreadTerminated, void());
    MOCK_METHOD0(willDestroyWorkerGlobalScope, void());
};

class WorkerThreadForTest : public WorkerThread {
public:
    WorkerThreadForTest(
        WorkerLoaderProxyProvider* mockWorkerLoaderProxyProvider,
        WorkerReportingProxy& mockWorkerReportingProxy)
        : WorkerThread(WorkerLoaderProxy::create(mockWorkerLoaderProxyProvider), mockWorkerReportingProxy)
        , m_thread(WebThreadSupportingGC::create("Test thread"))
        , m_scriptLoadedEvent(adoptPtr(new WaitableEvent()))
    {
        ASSERT(m_thread);
    }

    ~WorkerThreadForTest() override { }

    // WorkerThread implementation:
    WebThreadSupportingGC& backingThread() override
    {
        ASSERT(m_thread);
        return *m_thread;
    }
    void willDestroyIsolate() override
    {
        V8GCController::collectAllGarbageForTesting(v8::Isolate::GetCurrent());
        WorkerThread::willDestroyIsolate();
    }

    WorkerGlobalScope* createWorkerGlobalScope(PassOwnPtr<WorkerThreadStartupData>) override;

    void waitUntilScriptLoaded()
    {
        m_scriptLoadedEvent->wait();
    }

    void scriptLoaded()
    {
        m_scriptLoadedEvent->signal();
    }

    void startWithSourceCode(SecurityOrigin* securityOrigin, const String& source)
    {
        OwnPtr<Vector<CSPHeaderAndType>> headers = adoptPtr(new Vector<CSPHeaderAndType>());
        CSPHeaderAndType headerAndType("contentSecurityPolicy", ContentSecurityPolicyHeaderTypeReport);
        headers->append(headerAndType);

        WorkerClients* clients = nullptr;

        start(WorkerThreadStartupData::create(
            KURL(ParsedURLString, "http://fake.url/"),
            "fake user agent",
            source,
            nullptr,
            DontPauseWorkerGlobalScopeOnStart,
            headers.release(),
            securityOrigin,
            clients,
            WebAddressSpaceLocal,
            V8CacheOptionsDefault));
    }

    void waitForInit()
    {
        OwnPtr<WaitableEvent> completionEvent = adoptPtr(new WaitableEvent());
        backingThread().postTask(BLINK_FROM_HERE, threadSafeBind(&WaitableEvent::signal, AllowCrossThreadAccess(completionEvent.get())));
        completionEvent->wait();
    }

private:
    OwnPtr<WebThreadSupportingGC> m_thread;
    OwnPtr<WaitableEvent> m_scriptLoadedEvent;
};

class FakeWorkerGlobalScope : public WorkerGlobalScope {
public:
    FakeWorkerGlobalScope(const KURL& url, const String& userAgent, WorkerThreadForTest* thread, PassOwnPtr<SecurityOrigin::PrivilegeData> starterOriginPrivilegeData, WorkerClients* workerClients)
        : WorkerGlobalScope(url, userAgent, thread, monotonicallyIncreasingTime(), starterOriginPrivilegeData, workerClients)
        , m_thread(thread)
    {
    }

    ~FakeWorkerGlobalScope() override
    {
    }

    void scriptLoaded(size_t, size_t) override
    {
        m_thread->scriptLoaded();
    }

    // EventTarget
    const AtomicString& interfaceName() const override
    {
        return EventTargetNames::DedicatedWorkerGlobalScope;
    }

    void logExceptionToConsole(const String&, int, const String&, int, int, PassRefPtr<ScriptCallStack>) override
    {
    }

private:
    WorkerThreadForTest* m_thread;
};

inline WorkerGlobalScope* WorkerThreadForTest::createWorkerGlobalScope(PassOwnPtr<WorkerThreadStartupData> startupData)
{
    return new FakeWorkerGlobalScope(startupData->m_scriptURL, startupData->m_userAgent, this, startupData->m_starterOriginPrivilegeData.release(), startupData->m_workerClients.release());
}

} // namespace blink
