// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/devtools/devtools_manager.h"

#include <memory>

#include "base/location.h"
#include "base/macros.h"
#include "base/single_thread_task_runner.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "content/browser/devtools/shared_worker_devtools_manager.h"
#include "content/browser/shared_worker/shared_worker_instance.h"
#include "content/browser/shared_worker/worker_storage_partition.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_external_agent_proxy.h"
#include "content/public/browser/devtools_external_agent_proxy_delegate.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/test/test_utils.h"
#include "content/test/test_content_browser_client.h"
#include "content/test/test_render_view_host.h"
#include "content/test/test_web_contents.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::TimeDelta;

namespace content {
namespace {

class TestDevToolsClientHost : public DevToolsAgentHostClient {
 public:
  TestDevToolsClientHost()
      : last_sent_message(NULL),
        closed_(false) {
  }

  ~TestDevToolsClientHost() override { EXPECT_TRUE(closed_); }

  void Close() {
    EXPECT_FALSE(closed_);
    close_counter++;
    agent_host_->DetachClient();
    closed_ = true;
  }

  void AgentHostClosed(DevToolsAgentHost* agent_host, bool replaced) override {
    FAIL();
  }

  void DispatchProtocolMessage(DevToolsAgentHost* agent_host,
                               const std::string& message) override {
    last_sent_message = &message;
  }

  void InspectAgentHost(DevToolsAgentHost* agent_host) {
    agent_host_ = agent_host;
    agent_host_->AttachClient(this);
  }

  DevToolsAgentHost* agent_host() { return agent_host_.get(); }

  static void ResetCounters() {
    close_counter = 0;
  }

  static int close_counter;

  const std::string* last_sent_message;

 private:
  bool closed_;
  scoped_refptr<DevToolsAgentHost> agent_host_;

  DISALLOW_COPY_AND_ASSIGN(TestDevToolsClientHost);
};

int TestDevToolsClientHost::close_counter = 0;


class TestWebContentsDelegate : public WebContentsDelegate {
 public:
  TestWebContentsDelegate() : renderer_unresponsive_received_(false) {}

  // Notification that the contents is hung.
  void RendererUnresponsive(WebContents* source) override {
    renderer_unresponsive_received_ = true;
  }

  bool renderer_unresponsive_received() const {
    return renderer_unresponsive_received_;
  }

 private:
  bool renderer_unresponsive_received_;
};

}  // namespace

class DevToolsManagerTest : public RenderViewHostImplTestHarness {
 public:
  DevToolsManagerTest() {}

 protected:
  void SetUp() override {
    RenderViewHostImplTestHarness::SetUp();
    TestDevToolsClientHost::ResetCounters();
  }
};

TEST_F(DevToolsManagerTest, OpenAndManuallyCloseDevToolsClientHost) {
  scoped_refptr<DevToolsAgentHost> agent(
      DevToolsAgentHost::GetOrCreateFor(web_contents()));
  EXPECT_FALSE(agent->IsAttached());

  TestDevToolsClientHost client_host;
  client_host.InspectAgentHost(agent.get());
  // Test that the connection is established.
  EXPECT_TRUE(agent->IsAttached());
  EXPECT_EQ(0, TestDevToolsClientHost::close_counter);

  client_host.Close();
  EXPECT_EQ(1, TestDevToolsClientHost::close_counter);
  EXPECT_FALSE(agent->IsAttached());
}

TEST_F(DevToolsManagerTest, NoUnresponsiveDialogInInspectedContents) {
  const GURL url("http://www.google.com");
  contents()->NavigateAndCommit(url);
  TestRenderViewHost* inspected_rvh = test_rvh();
  EXPECT_TRUE(inspected_rvh->IsRenderViewLive());
  EXPECT_FALSE(contents()->GetDelegate());
  TestWebContentsDelegate delegate;
  contents()->SetDelegate(&delegate);

  TestDevToolsClientHost client_host;
  scoped_refptr<DevToolsAgentHost> agent_host(DevToolsAgentHost::GetOrCreateFor(
      WebContents::FromRenderViewHost(inspected_rvh)));
  client_host.InspectAgentHost(agent_host.get());

  // Start with a short timeout.
  inspected_rvh->GetWidget()->StartHangMonitorTimeout(
      TimeDelta::FromMilliseconds(10));
  // Wait long enough for first timeout and see if it fired.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::MessageLoop::QuitWhenIdleClosure(),
      TimeDelta::FromMilliseconds(10));
  base::MessageLoop::current()->Run();
  EXPECT_FALSE(delegate.renderer_unresponsive_received());

  // Now close devtools and check that the notification is delivered.
  client_host.Close();
  // Start with a short timeout.
  inspected_rvh->GetWidget()->StartHangMonitorTimeout(
      TimeDelta::FromMilliseconds(10));
  // Wait long enough for first timeout and see if it fired.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::MessageLoop::QuitWhenIdleClosure(),
      TimeDelta::FromMilliseconds(10));
  base::MessageLoop::current()->Run();
  EXPECT_TRUE(delegate.renderer_unresponsive_received());

  contents()->SetDelegate(NULL);
}

TEST_F(DevToolsManagerTest, ReattachOnCancelPendingNavigation) {
  // Navigate to URL.  First URL should use first RenderViewHost.
  const GURL url("http://www.google.com");
  controller().LoadURL(
      url, Referrer(), ui::PAGE_TRANSITION_TYPED, std::string());
  int pending_id = controller().GetPendingEntry()->GetUniqueID();
  contents()->GetMainFrame()->PrepareForCommit();
  contents()->TestDidNavigate(contents()->GetMainFrame(), 1, pending_id, true,
                              url, ui::PAGE_TRANSITION_TYPED);
  contents()->GetMainFrame()->SimulateNavigationStop();
  EXPECT_FALSE(contents()->CrossProcessNavigationPending());

  TestDevToolsClientHost client_host;
  client_host.InspectAgentHost(
      DevToolsAgentHost::GetOrCreateFor(web_contents()).get());

  // Navigate to new site which should get a new RenderViewHost.
  const GURL url2("http://www.yahoo.com");
  controller().LoadURL(
      url2, Referrer(), ui::PAGE_TRANSITION_TYPED, std::string());
  contents()->GetMainFrame()->PrepareForCommit();
  EXPECT_TRUE(contents()->CrossProcessNavigationPending());
  EXPECT_EQ(client_host.agent_host(),
            DevToolsAgentHost::GetOrCreateFor(web_contents()).get());

  // Interrupt pending navigation and navigate back to the original site.
  controller().LoadURL(
      url, Referrer(), ui::PAGE_TRANSITION_TYPED, std::string());
  pending_id = controller().GetPendingEntry()->GetUniqueID();
  contents()->GetMainFrame()->PrepareForCommit();
  contents()->TestDidNavigate(contents()->GetMainFrame(), 1, pending_id, false,
                              url, ui::PAGE_TRANSITION_TYPED);
  EXPECT_FALSE(contents()->CrossProcessNavigationPending());
  EXPECT_EQ(client_host.agent_host(),
            DevToolsAgentHost::GetOrCreateFor(web_contents()).get());
  client_host.Close();
}

class TestExternalAgentDelegate: public DevToolsExternalAgentProxyDelegate {
  std::map<std::string,int> event_counter_;

  void recordEvent(const std::string& name) {
    if (event_counter_.find(name) == event_counter_.end())
      event_counter_[name] = 0;
    event_counter_[name] = event_counter_[name] + 1;
  }

  void expectEvent(int count, const std::string& name) {
    EXPECT_EQ(count, event_counter_[name]);
  }

  void Attach(DevToolsExternalAgentProxy* proxy) override {
    recordEvent("Attach");
  };

  void Detach() override { recordEvent("Detach"); };

  void SendMessageToBackend(const std::string& message) override {
    recordEvent(std::string("SendMessageToBackend.") + message);
  };

 public :
  ~TestExternalAgentDelegate() override {
    expectEvent(1, "Attach");
    expectEvent(1, "Detach");
    expectEvent(0, "SendMessageToBackend.message0");
    expectEvent(1, "SendMessageToBackend.message1");
    expectEvent(2, "SendMessageToBackend.message2");
  }
};

TEST_F(DevToolsManagerTest, TestExternalProxy) {
  TestExternalAgentDelegate* delegate = new TestExternalAgentDelegate();

  scoped_refptr<DevToolsAgentHost> agent_host =
      DevToolsAgentHost::Create(delegate);
  EXPECT_EQ(agent_host, DevToolsAgentHost::GetForId(agent_host->GetId()));

  TestDevToolsClientHost client_host;
  client_host.InspectAgentHost(agent_host.get());
  agent_host->DispatchProtocolMessage("message1");
  agent_host->DispatchProtocolMessage("message2");
  agent_host->DispatchProtocolMessage("message2");

  client_host.Close();
}

}  // namespace content
