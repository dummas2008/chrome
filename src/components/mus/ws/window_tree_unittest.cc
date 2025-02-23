// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/stringprintf.h"
#include "components/mus/common/types.h"
#include "components/mus/common/util.h"
#include "components/mus/public/interfaces/window_tree.mojom.h"
#include "components/mus/surfaces/surfaces_state.h"
#include "components/mus/ws/default_access_policy.h"
#include "components/mus/ws/display_binding.h"
#include "components/mus/ws/ids.h"
#include "components/mus/ws/platform_display.h"
#include "components/mus/ws/platform_display_factory.h"
#include "components/mus/ws/platform_display_init_params.h"
#include "components/mus/ws/server_window.h"
#include "components/mus/ws/server_window_surface_manager_test_api.h"
#include "components/mus/ws/test_change_tracker.h"
#include "components/mus/ws/test_server_window_delegate.h"
#include "components/mus/ws/test_utils.h"
#include "components/mus/ws/window_manager_access_policy.h"
#include "components/mus/ws/window_server.h"
#include "components/mus/ws/window_server_delegate.h"
#include "components/mus/ws/window_tree.h"
#include "components/mus/ws/window_tree_binding.h"
#include "mojo/converters/geometry/geometry_type_converters.h"
#include "mojo/shell/public/interfaces/connector.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/geometry/rect.h"

namespace mus {
namespace ws {
namespace test {
namespace {

std::string WindowIdToString(const WindowId& id) {
  return base::StringPrintf("%d,%d", id.connection_id, id.window_id);
}

ClientWindowId BuildClientWindowId(WindowTree* tree,
                                   ConnectionSpecificId window_id) {
  return ClientWindowId(WindowIdToTransportId(WindowId(tree->id(), window_id)));
}

// -----------------------------------------------------------------------------

ui::PointerEvent CreatePointerDownEvent(int x, int y) {
  return ui::PointerEvent(ui::TouchEvent(ui::ET_TOUCH_PRESSED, gfx::Point(x, y),
                                         1, ui::EventTimeForNow()));
}

ui::PointerEvent CreatePointerUpEvent(int x, int y) {
  return ui::PointerEvent(ui::TouchEvent(
      ui::ET_TOUCH_RELEASED, gfx::Point(x, y), 1, ui::EventTimeForNow()));
}

ui::PointerEvent CreateMouseMoveEvent(int x, int y) {
  return ui::PointerEvent(
      ui::MouseEvent(ui::ET_MOUSE_MOVED, gfx::Point(x, y), gfx::Point(x, y),
                     ui::EventTimeForNow(), ui::EF_NONE, ui::EF_NONE));
}

ui::PointerEvent CreateMouseDownEvent(int x, int y) {
  return ui::PointerEvent(
      ui::MouseEvent(ui::ET_MOUSE_PRESSED, gfx::Point(x, y), gfx::Point(x, y),
                     ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                     ui::EF_LEFT_MOUSE_BUTTON));
}

ui::PointerEvent CreateMouseUpEvent(int x, int y) {
  return ui::PointerEvent(
      ui::MouseEvent(ui::ET_MOUSE_RELEASED, gfx::Point(x, y), gfx::Point(x, y),
                     ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                     ui::EF_LEFT_MOUSE_BUTTON));
}

ServerWindow* GetCaptureWindow(Display* display) {
  return display->GetActiveWindowManagerState()->capture_window();
}

}  // namespace

// -----------------------------------------------------------------------------

class WindowTreeTest : public testing::Test {
 public:
  WindowTreeTest()
      : wm_client_(nullptr),
        cursor_id_(0),
        platform_display_factory_(&cursor_id_),
        surfaces_state_(new SurfacesState()) {}
  ~WindowTreeTest() override {}

  // WindowTree for the window manager.
  WindowTree* wm_tree() { return window_server_->GetTreeWithId(1); }

  TestWindowTreeClient* last_window_tree_client() {
    return delegate_.last_client();
  }

  TestWindowTreeBinding* last_binding() { return delegate_.last_binding(); }

  WindowServer* window_server() { return window_server_.get(); }

  ServerWindow* GetWindowById(const WindowId& id) {
    return window_server_->GetWindow(id);
  }

  TestWindowTreeClient* wm_client() { return wm_client_; }
  mus::mojom::Cursor cursor_id() {
    return static_cast<mus::mojom::Cursor>(cursor_id_);
  }

  void DispatchEventWithoutAck(const ui::Event& event) {
    DisplayTestApi(display_).OnEvent(event);
  }

  void set_window_manager_internal(WindowTree* tree,
                                   mojom::WindowManager* wm_internal) {
    WindowTreeTestApi(tree).set_window_manager_internal(wm_internal);
  }

  void AckPreviousEvent() {
    WindowManagerStateTestApi test_api(display_->GetActiveWindowManagerState());
    while (test_api.tree_awaiting_input_ack())
      test_api.tree_awaiting_input_ack()->OnWindowInputEventAck(0, true);
  }

  void DispatchEventAndAckImmediately(const ui::Event& event) {
    DispatchEventWithoutAck(event);
    AckPreviousEvent();
  }

  // Creates a new window from wm_tree() and embeds a new connection in
  // it.
  void SetupEventTargeting(TestWindowTreeClient** out_client,
                           WindowTree** window_tree,
                           ServerWindow** window);

  // Creates a new tree as the specified user. This does what creation via
  // a WindowTreeFactory does.
  WindowTree* CreateNewTree(const UserId& user_id,
                            TestWindowTreeBinding** binding) {
    WindowTree* tree = new WindowTree(window_server_.get(), user_id, nullptr,
                                      make_scoped_ptr(new DefaultAccessPolicy));
    *binding = new TestWindowTreeBinding(tree);
    window_server_->AddTree(make_scoped_ptr(tree), make_scoped_ptr(*binding),
                            nullptr);
    return tree;
  }

 protected:
  // testing::Test:
  void SetUp() override {
    PlatformDisplay::set_factory_for_testing(&platform_display_factory_);
    window_server_.reset(new WindowServer(&delegate_, surfaces_state_));
    PlatformDisplayInitParams display_init_params;
    display_init_params.surfaces_state = surfaces_state_;
    display_ = new Display(window_server_.get(), display_init_params);
    display_binding_ = new TestDisplayBinding(display_, window_server_.get());
    display_->Init(make_scoped_ptr(display_binding_));
    wm_client_ = delegate_.last_client();
  }

 protected:
  // TestWindowTreeClient that is used for the WM connection.
  TestWindowTreeClient* wm_client_;
  int32_t cursor_id_;
  TestPlatformDisplayFactory platform_display_factory_;
  TestWindowServerDelegate delegate_;
  // Owned by WindowServer.
  TestDisplayBinding* display_binding_;
  Display* display_ = nullptr;
  scoped_refptr<SurfacesState> surfaces_state_;
  scoped_ptr<WindowServer> window_server_;
  base::MessageLoop message_loop_;

  DISALLOW_COPY_AND_ASSIGN(WindowTreeTest);
};

// Creates a new window in wm_tree(), adds it to the root, embeds a
// new client in the window and creates a child of said window. |window| is
// set to the child of |window_tree| that is created.
void WindowTreeTest::SetupEventTargeting(TestWindowTreeClient** out_client,
                                         WindowTree** window_tree,
                                         ServerWindow** window) {
  const ClientWindowId embed_window_id = BuildClientWindowId(wm_tree(), 1);
  EXPECT_TRUE(
      wm_tree()->NewWindow(embed_window_id, ServerWindow::Properties()));
  EXPECT_TRUE(wm_tree()->SetWindowVisibility(embed_window_id, true));
  EXPECT_TRUE(wm_tree()->AddWindow(FirstRootId(wm_tree()), embed_window_id));
  display_->root_window()->SetBounds(gfx::Rect(0, 0, 100, 100));
  mojom::WindowTreeClientPtr client;
  mojom::WindowTreeClientRequest client_request = GetProxy(&client);
  wm_client()->Bind(std::move(client_request));
  wm_tree()->Embed(embed_window_id, std::move(client));
  ServerWindow* embed_window = wm_tree()->GetWindowByClientId(embed_window_id);
  WindowTree* tree1 = window_server()->GetTreeWithRoot(embed_window);
  ASSERT_TRUE(tree1 != nullptr);
  ASSERT_NE(tree1, wm_tree());

  embed_window->SetBounds(gfx::Rect(0, 0, 50, 50));

  const ClientWindowId child1_id(BuildClientWindowId(tree1, 1));
  EXPECT_TRUE(tree1->NewWindow(child1_id, ServerWindow::Properties()));
  ServerWindow* child1 = tree1->GetWindowByClientId(child1_id);
  ASSERT_TRUE(child1);
  EXPECT_TRUE(tree1->AddWindow(ClientWindowIdForWindow(tree1, embed_window),
                               child1_id));
  tree1->GetDisplay(embed_window)->AddActivationParent(embed_window);

  child1->SetVisible(true);
  child1->SetBounds(gfx::Rect(20, 20, 20, 20));
  EnableHitTest(child1);

  TestWindowTreeClient* embed_connection = last_window_tree_client();
  embed_connection->tracker()->changes()->clear();
  wm_client()->tracker()->changes()->clear();

  *out_client = embed_connection;
  *window_tree = tree1;
  *window = child1;
}

// Verifies focus correctly changes on pointer events.
TEST_F(WindowTreeTest, FocusOnPointer) {
  const ClientWindowId embed_window_id = BuildClientWindowId(wm_tree(), 1);
  EXPECT_TRUE(
      wm_tree()->NewWindow(embed_window_id, ServerWindow::Properties()));
  ServerWindow* embed_window = wm_tree()->GetWindowByClientId(embed_window_id);
  ASSERT_TRUE(embed_window);
  EXPECT_TRUE(wm_tree()->SetWindowVisibility(embed_window_id, true));
  ASSERT_TRUE(FirstRoot(wm_tree()));
  const ClientWindowId wm_root_id = FirstRootId(wm_tree());
  EXPECT_TRUE(wm_tree()->AddWindow(wm_root_id, embed_window_id));
  display_->root_window()->SetBounds(gfx::Rect(0, 0, 100, 100));
  mojom::WindowTreeClientPtr client;
  mojom::WindowTreeClientRequest client_request = GetProxy(&client);
  wm_client()->Bind(std::move(client_request));
  wm_tree()->Embed(embed_window_id, std::move(client));
  WindowTree* tree1 = window_server()->GetTreeWithRoot(embed_window);
  ASSERT_TRUE(tree1 != nullptr);
  ASSERT_NE(tree1, wm_tree());

  embed_window->SetBounds(gfx::Rect(0, 0, 50, 50));

  const ClientWindowId child1_id(BuildClientWindowId(tree1, 1));
  EXPECT_TRUE(tree1->NewWindow(child1_id, ServerWindow::Properties()));
  EXPECT_TRUE(tree1->AddWindow(ClientWindowIdForWindow(tree1, embed_window),
                               child1_id));
  ServerWindow* child1 = tree1->GetWindowByClientId(child1_id);
  ASSERT_TRUE(child1);
  child1->SetVisible(true);
  child1->SetBounds(gfx::Rect(20, 20, 20, 20));
  EnableHitTest(child1);

  TestWindowTreeClient* tree1_client = last_window_tree_client();
  tree1_client->tracker()->changes()->clear();
  wm_client()->tracker()->changes()->clear();

  // Focus should not go to |child1| yet, since the parent still doesn't allow
  // active children.
  DispatchEventAndAckImmediately(CreatePointerDownEvent(21, 22));
  Display* display1 = tree1->GetDisplay(embed_window);
  EXPECT_EQ(nullptr, display1->GetFocusedWindow());
  DispatchEventAndAckImmediately(CreatePointerUpEvent(21, 22));
  tree1_client->tracker()->changes()->clear();
  wm_client()->tracker()->changes()->clear();

  display1->AddActivationParent(embed_window);

  // Focus should go to child1. This result in notifying both the window
  // manager and client connection being notified.
  DispatchEventAndAckImmediately(CreatePointerDownEvent(21, 22));
  EXPECT_EQ(child1, display1->GetFocusedWindow());
  ASSERT_GE(wm_client()->tracker()->changes()->size(), 1u);
  EXPECT_EQ("Focused id=2,1",
            ChangesToDescription1(*wm_client()->tracker()->changes())[0]);
  ASSERT_GE(tree1_client->tracker()->changes()->size(), 1u);
  EXPECT_EQ("Focused id=2,1",
            ChangesToDescription1(*tree1_client->tracker()->changes())[0]);

  DispatchEventAndAckImmediately(CreatePointerUpEvent(21, 22));
  wm_client()->tracker()->changes()->clear();
  tree1_client->tracker()->changes()->clear();

  // Press outside of the embedded window. Note that root cannot be focused
  // (because it cannot be activated). So the focus would not move in this case.
  DispatchEventAndAckImmediately(CreatePointerDownEvent(61, 22));
  EXPECT_EQ(child1, display_->GetFocusedWindow());

  DispatchEventAndAckImmediately(CreatePointerUpEvent(21, 22));
  wm_client()->tracker()->changes()->clear();
  tree1_client->tracker()->changes()->clear();

  // Press in the same location. Should not get a focus change event (only input
  // event).
  DispatchEventAndAckImmediately(CreatePointerDownEvent(61, 22));
  EXPECT_EQ(child1, display_->GetFocusedWindow());
  ASSERT_EQ(wm_client()->tracker()->changes()->size(), 1u)
      << SingleChangeToDescription(*wm_client()->tracker()->changes());
  EXPECT_EQ("InputEvent window=0,3 event_action=4",
            ChangesToDescription1(*wm_client()->tracker()->changes())[0]);
  EXPECT_TRUE(tree1_client->tracker()->changes()->empty());
}

TEST_F(WindowTreeTest, BasicInputEventTarget) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));

  // Send an event to |v1|. |embed_connection| should get the event, not
  // |wm_client|, since |v1| lives inside an embedded window.
  DispatchEventAndAckImmediately(CreatePointerDownEvent(21, 22));
  ASSERT_EQ(1u, wm_client()->tracker()->changes()->size());
  EXPECT_EQ("Focused id=2,1",
            ChangesToDescription1(*wm_client()->tracker()->changes())[0]);
  ASSERT_EQ(2u, embed_connection->tracker()->changes()->size());
  EXPECT_EQ("Focused id=2,1",
            ChangesToDescription1(*embed_connection->tracker()->changes())[0]);
  EXPECT_EQ("InputEvent window=2,1 event_action=4",
            ChangesToDescription1(*embed_connection->tracker()->changes())[1]);
}

TEST_F(WindowTreeTest, CursorChangesWhenMouseOverWindowAndWindowSetsCursor) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));

  // Like in BasicInputEventTarget, we send a pointer down event to be
  // dispatched. This is only to place the mouse cursor over that window though.
  DispatchEventAndAckImmediately(CreateMouseMoveEvent(21, 22));

  window->SetPredefinedCursor(mojom::Cursor::IBEAM);

  // Because the cursor is over the window when SetCursor was called, we should
  // have immediately changed the cursor.
  EXPECT_EQ(mojom::Cursor::IBEAM, cursor_id());
}

TEST_F(WindowTreeTest, CursorChangesWhenEnteringWindowWithDifferentCursor) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));

  // Let's create a pointer event outside the window and then move the pointer
  // inside.
  DispatchEventAndAckImmediately(CreateMouseMoveEvent(5, 5));
  window->SetPredefinedCursor(mojom::Cursor::IBEAM);
  EXPECT_EQ(mojom::Cursor::CURSOR_NULL, cursor_id());

  DispatchEventAndAckImmediately(CreateMouseMoveEvent(21, 22));
  EXPECT_EQ(mojom::Cursor::IBEAM, cursor_id());
}

TEST_F(WindowTreeTest, TouchesDontChangeCursor) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));

  // Let's create a pointer event outside the window and then move the pointer
  // inside.
  DispatchEventAndAckImmediately(CreateMouseMoveEvent(5, 5));
  window->SetPredefinedCursor(mojom::Cursor::IBEAM);
  EXPECT_EQ(mojom::Cursor::CURSOR_NULL, cursor_id());

  // With a touch event, we shouldn't update the cursor.
  DispatchEventAndAckImmediately(CreatePointerDownEvent(21, 22));
  EXPECT_EQ(mojom::Cursor::CURSOR_NULL, cursor_id());
}

TEST_F(WindowTreeTest, DragOutsideWindow) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));

  // Start with the cursor outside the window. Setting the cursor shouldn't
  // change the cursor.
  DispatchEventAndAckImmediately(CreateMouseMoveEvent(5, 5));
  window->SetPredefinedCursor(mojom::Cursor::IBEAM);
  EXPECT_EQ(mojom::Cursor::CURSOR_NULL, cursor_id());

  // Move the pointer to the inside of the window
  DispatchEventAndAckImmediately(CreateMouseMoveEvent(21, 22));
  EXPECT_EQ(mojom::Cursor::IBEAM, cursor_id());

  // Start the drag.
  DispatchEventAndAckImmediately(CreateMouseDownEvent(21, 22));
  EXPECT_EQ(mojom::Cursor::IBEAM, cursor_id());

  // Move the cursor (mouse is still down) outside the window.
  DispatchEventAndAckImmediately(CreateMouseMoveEvent(5, 5));
  EXPECT_EQ(mojom::Cursor::IBEAM, cursor_id());

  // Release the cursor. We should now adapt the cursor of the window
  // underneath the pointer.
  DispatchEventAndAckImmediately(CreateMouseUpEvent(5, 5));
  EXPECT_EQ(mojom::Cursor::CURSOR_NULL, cursor_id());
}

TEST_F(WindowTreeTest, ChangingWindowBoundsChangesCursor) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));

  // Put the cursor just outside the bounds of the window.
  DispatchEventAndAckImmediately(CreateMouseMoveEvent(41, 41));
  window->SetPredefinedCursor(mojom::Cursor::IBEAM);
  EXPECT_EQ(mojom::Cursor::CURSOR_NULL, cursor_id());

  // Expand the bounds of the window so they now include where the cursor now
  // is.
  window->SetBounds(gfx::Rect(20, 20, 25, 25));
  EXPECT_EQ(mojom::Cursor::IBEAM, cursor_id());

  // Contract the bounds again.
  window->SetBounds(gfx::Rect(20, 20, 20, 20));
  EXPECT_EQ(mojom::Cursor::CURSOR_NULL, cursor_id());
}

TEST_F(WindowTreeTest, WindowReorderingChangesCursor) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window1 = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window1));

  // Create a second window right over the first.
  const ClientWindowId embed_window_id(FirstRootId(tree));
  const ClientWindowId child2_id(BuildClientWindowId(tree, 2));
  EXPECT_TRUE(tree->NewWindow(child2_id, ServerWindow::Properties()));
  ServerWindow* child2 = tree->GetWindowByClientId(child2_id);
  ASSERT_TRUE(child2);
  EXPECT_TRUE(tree->AddWindow(embed_window_id, child2_id));
  child2->SetVisible(true);
  child2->SetBounds(gfx::Rect(20, 20, 20, 20));
  EnableHitTest(child2);

  // Give each window a different cursor.
  window1->SetPredefinedCursor(mojom::Cursor::IBEAM);
  child2->SetPredefinedCursor(mojom::Cursor::HAND);

  // We expect window2 to be over window1 now.
  DispatchEventAndAckImmediately(CreateMouseMoveEvent(22, 22));
  EXPECT_EQ(mojom::Cursor::HAND, cursor_id());

  // But when we put window2 at the bottom, we should adapt window1's cursor.
  child2->parent()->StackChildAtBottom(child2);
  EXPECT_EQ(mojom::Cursor::IBEAM, cursor_id());
}

TEST_F(WindowTreeTest, EventAck) {
  const ClientWindowId embed_window_id = BuildClientWindowId(wm_tree(), 1);
  EXPECT_TRUE(
      wm_tree()->NewWindow(embed_window_id, ServerWindow::Properties()));
  EXPECT_TRUE(wm_tree()->SetWindowVisibility(embed_window_id, true));
  ASSERT_TRUE(FirstRoot(wm_tree()));
  EXPECT_TRUE(wm_tree()->AddWindow(FirstRootId(wm_tree()), embed_window_id));
  display_->root_window()->SetBounds(gfx::Rect(0, 0, 100, 100));

  wm_client()->tracker()->changes()->clear();
  DispatchEventWithoutAck(CreateMouseMoveEvent(21, 22));
  ASSERT_EQ(1u, wm_client()->tracker()->changes()->size());
  EXPECT_EQ("InputEvent window=0,3 event_action=5",
            ChangesToDescription1(*wm_client()->tracker()->changes())[0]);
  wm_client()->tracker()->changes()->clear();

  // Send another event. This event shouldn't reach the client.
  DispatchEventWithoutAck(CreateMouseMoveEvent(21, 22));
  ASSERT_EQ(0u, wm_client()->tracker()->changes()->size());

  // Ack the first event. That should trigger the dispatch of the second event.
  AckPreviousEvent();
  ASSERT_EQ(1u, wm_client()->tracker()->changes()->size());
  EXPECT_EQ("InputEvent window=0,3 event_action=5",
            ChangesToDescription1(*wm_client()->tracker()->changes())[0]);
}

// Establish connection, call NewTopLevelWindow(), make sure get id, and make
// sure client paused.
TEST_F(WindowTreeTest, NewTopLevelWindow) {
  TestWindowManager wm_internal;
  set_window_manager_internal(wm_tree(), &wm_internal);

  TestWindowTreeBinding* child_binding;
  WindowTree* child_tree = CreateNewTree(wm_tree()->user_id(), &child_binding);
  child_binding->client()->tracker()->changes()->clear();
  child_binding->client()->set_record_on_change_completed(true);

  // Create a new top level window.
  mojo::Map<mojo::String, mojo::Array<uint8_t>> properties;
  const uint32_t initial_change_id = 17;
  // Explicitly use an id that does not contain the connection id.
  const ClientWindowId embed_window_id2_in_child(45 << 16 | 27);
  static_cast<mojom::WindowTree*>(child_tree)
      ->NewTopLevelWindow(initial_change_id, embed_window_id2_in_child.id,
                          std::move(properties));

  // The binding should be paused until the wm acks the change.
  uint32_t wm_change_id = 0u;
  ASSERT_TRUE(wm_internal.did_call_create_top_level_window(&wm_change_id));
  EXPECT_TRUE(child_binding->is_paused());

  // Create the window for |embed_window_id2_in_child|.
  const ClientWindowId embed_window_id2 = BuildClientWindowId(wm_tree(), 2);
  EXPECT_TRUE(
      wm_tree()->NewWindow(embed_window_id2, ServerWindow::Properties()));
  EXPECT_TRUE(wm_tree()->SetWindowVisibility(embed_window_id2, true));
  EXPECT_TRUE(wm_tree()->AddWindow(FirstRootId(wm_tree()), embed_window_id2));

  // Ack the change, which should resume the binding.
  child_binding->client()->tracker()->changes()->clear();
  static_cast<mojom::WindowManagerClient*>(wm_tree())
      ->OnWmCreatedTopLevelWindow(wm_change_id, embed_window_id2.id);
  EXPECT_FALSE(child_binding->is_paused());
  EXPECT_EQ("TopLevelCreated id=17 window_id=" +
                WindowIdToString(
                    WindowIdFromTransportId(embed_window_id2_in_child.id)) +
                " drawn=true",
            SingleChangeToDescription(
                *child_binding->client()->tracker()->changes()));
  child_binding->client()->tracker()->changes()->clear();

  // Change the visibility of the window from the owner and make sure the
  // client sees the right id.
  ServerWindow* embed_window = wm_tree()->GetWindowByClientId(embed_window_id2);
  ASSERT_TRUE(embed_window);
  EXPECT_TRUE(embed_window->visible());
  ASSERT_TRUE(wm_tree()->SetWindowVisibility(
      ClientWindowIdForWindow(wm_tree(), embed_window), false));
  EXPECT_FALSE(embed_window->visible());
  EXPECT_EQ("VisibilityChanged window=" +
                WindowIdToString(
                    WindowIdFromTransportId(embed_window_id2_in_child.id)) +
                " visible=false",
            SingleChangeToDescription(
                *child_binding->client()->tracker()->changes()));

  // Set the visibility from the child using the client assigned id.
  ASSERT_TRUE(child_tree->SetWindowVisibility(embed_window_id2_in_child, true));
  EXPECT_TRUE(embed_window->visible());
}

// Tests that setting capture only works while an input event is being
// processed, and the only the capture window can release capture.
TEST_F(WindowTreeTest, ExplicitSetCapture) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));
  const ServerWindow* root_window = *tree->roots().begin();
  tree->AddWindow(FirstRootId(tree), ClientWindowIdForWindow(tree, window));
  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  ASSERT_TRUE(tree->GetDisplay(window));

  // Setting capture should fail when there are no active events
  mojom::WindowTree* mojom_window_tree = static_cast<mojom::WindowTree*>(tree);
  uint32_t change_id = 42;
  mojom_window_tree->SetCapture(change_id, WindowIdToTransportId(window->id()));
  Display* display = tree->GetDisplay(window);
  EXPECT_NE(window, GetCaptureWindow(display));

  // Setting capture after the event is acknowledged should fail
  DispatchEventAndAckImmediately(CreatePointerDownEvent(10, 10));
  mojom_window_tree->SetCapture(++change_id,
                                WindowIdToTransportId(window->id()));
  EXPECT_NE(window, GetCaptureWindow(display));

  // Settings while the event is being process should pass
  DispatchEventWithoutAck(CreatePointerDownEvent(10, 10));
  mojom_window_tree->SetCapture(++change_id,
                                WindowIdToTransportId(window->id()));
  EXPECT_EQ(window, GetCaptureWindow(display));
  AckPreviousEvent();

  // Only the capture window should be able to release capture
  mojom_window_tree->ReleaseCapture(++change_id,
                                    WindowIdToTransportId(root_window->id()));
  EXPECT_EQ(window, GetCaptureWindow(display));
  mojom_window_tree->ReleaseCapture(++change_id,
                                    WindowIdToTransportId(window->id()));
  EXPECT_EQ(nullptr, GetCaptureWindow(display));
}

// Tests that while a client is interacting with input, that capture is not
// allowed for invisible windows.
TEST_F(WindowTreeTest, CaptureWindowMustBeVisible) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));
  tree->AddWindow(FirstRootId(tree), ClientWindowIdForWindow(tree, window));
  window->SetBounds(gfx::Rect(0, 0, 100, 100));
  ASSERT_TRUE(tree->GetDisplay(window));

  DispatchEventWithoutAck(CreatePointerDownEvent(10, 10));
  window->SetVisible(false);
  EXPECT_FALSE(tree->SetCapture(ClientWindowIdForWindow(tree, window)));
  EXPECT_NE(window, GetCaptureWindow(tree->GetDisplay(window)));
}

// Tests that showing a modal window releases the capture if the capture is on a
// descendant of the modal parent.
TEST_F(WindowTreeTest, ShowModalWindowWithDescendantCapture) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* w1 = nullptr;
  EXPECT_NO_FATAL_FAILURE(SetupEventTargeting(&embed_connection, &tree, &w1));

  w1->SetBounds(gfx::Rect(10, 10, 30, 30));
  const ServerWindow* root_window = *tree->roots().begin();
  ClientWindowId root_window_id = ClientWindowIdForWindow(tree, root_window);
  ClientWindowId w1_id = ClientWindowIdForWindow(tree, w1);
  Display* display = tree->GetDisplay(w1);

  // Create |w11| as a child of |w1| and make it visible.
  ClientWindowId w11_id = BuildClientWindowId(tree, 11);
  ASSERT_TRUE(tree->NewWindow(w11_id, ServerWindow::Properties()));
  ServerWindow* w11 = tree->GetWindowByClientId(w11_id);
  w11->SetBounds(gfx::Rect(10, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(w1_id, w11_id));
  ASSERT_TRUE(tree->SetWindowVisibility(w11_id, true));

  // Create |w2| as a child of |root_window| and modal to |w1| and leave it
  // hidden.
  ClientWindowId w2_id = BuildClientWindowId(tree, 2);
  ASSERT_TRUE(tree->NewWindow(w2_id, ServerWindow::Properties()));
  ServerWindow* w2 = tree->GetWindowByClientId(w2_id);
  w2->SetBounds(gfx::Rect(50, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(root_window_id, w2_id));
  ASSERT_TRUE(tree->AddTransientWindow(w1_id, w2_id));
  ASSERT_TRUE(tree->SetModal(w2_id));

  // Set capture to |w11|.
  DispatchEventWithoutAck(CreatePointerDownEvent(25, 25));
  ASSERT_TRUE(tree->SetCapture(w11_id));
  EXPECT_EQ(w11, GetCaptureWindow(display));
  AckPreviousEvent();

  // Make |w2| visible. This should release capture as capture is set to a
  // descendant of the modal parent.
  ASSERT_TRUE(tree->SetWindowVisibility(w2_id, true));
  EXPECT_EQ(nullptr, GetCaptureWindow(display));
}

// Tests that setting a visible window as modal releases the capture if the
// capture is on a descendant of the modal parent.
TEST_F(WindowTreeTest, VisibleWindowToModalWithDescendantCapture) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* w1 = nullptr;
  EXPECT_NO_FATAL_FAILURE(SetupEventTargeting(&embed_connection, &tree, &w1));

  w1->SetBounds(gfx::Rect(10, 10, 30, 30));
  const ServerWindow* root_window = *tree->roots().begin();
  ClientWindowId root_window_id = ClientWindowIdForWindow(tree, root_window);
  ClientWindowId w1_id = ClientWindowIdForWindow(tree, w1);
  Display* display = tree->GetDisplay(w1);

  // Create |w11| as a child of |w1| and make it visible.
  ClientWindowId w11_id = BuildClientWindowId(tree, 11);
  ASSERT_TRUE(tree->NewWindow(w11_id, ServerWindow::Properties()));
  ServerWindow* w11 = tree->GetWindowByClientId(w11_id);
  w11->SetBounds(gfx::Rect(10, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(w1_id, w11_id));
  ASSERT_TRUE(tree->SetWindowVisibility(w11_id, true));

  // Create |w2| as a child of |root_window| and make it visible.
  ClientWindowId w2_id = BuildClientWindowId(tree, 2);
  ASSERT_TRUE(tree->NewWindow(w2_id, ServerWindow::Properties()));
  ServerWindow* w2 = tree->GetWindowByClientId(w2_id);
  w2->SetBounds(gfx::Rect(50, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(root_window_id, w2_id));
  ASSERT_TRUE(tree->SetWindowVisibility(w2_id, true));

  // Set capture to |w11|.
  DispatchEventWithoutAck(CreatePointerDownEvent(25, 25));
  ASSERT_TRUE(tree->SetCapture(w11_id));
  EXPECT_EQ(w11, GetCaptureWindow(display));
  AckPreviousEvent();

  // Set |w2| modal to |w1|. This should release the capture as the capture is
  // set to a descendant of the modal parent.
  ASSERT_TRUE(tree->AddTransientWindow(w1_id, w2_id));
  ASSERT_TRUE(tree->SetModal(w2_id));
  EXPECT_EQ(nullptr, GetCaptureWindow(display));
}

// Tests that showing a modal window does not change capture if the capture is
// not on a descendant of the modal parent.
TEST_F(WindowTreeTest, ShowModalWindowWithNonDescendantCapture) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* w1 = nullptr;
  EXPECT_NO_FATAL_FAILURE(SetupEventTargeting(&embed_connection, &tree, &w1));

  w1->SetBounds(gfx::Rect(10, 10, 30, 30));
  const ServerWindow* root_window = *tree->roots().begin();
  ClientWindowId root_window_id = ClientWindowIdForWindow(tree, root_window);
  ClientWindowId w1_id = ClientWindowIdForWindow(tree, w1);
  Display* display = tree->GetDisplay(w1);

  // Create |w2| as a child of |root_window| and modal to |w1| and leave it
  // hidden..
  ClientWindowId w2_id = BuildClientWindowId(tree, 2);
  ASSERT_TRUE(tree->NewWindow(w2_id, ServerWindow::Properties()));
  ServerWindow* w2 = tree->GetWindowByClientId(w2_id);
  w2->SetBounds(gfx::Rect(50, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(root_window_id, w2_id));
  ASSERT_TRUE(tree->AddTransientWindow(w1_id, w2_id));
  ASSERT_TRUE(tree->SetModal(w2_id));

  // Create |w3| as a child of |root_window| and make it visible.
  ClientWindowId w3_id = BuildClientWindowId(tree, 3);
  ASSERT_TRUE(tree->NewWindow(w3_id, ServerWindow::Properties()));
  ServerWindow* w3 = tree->GetWindowByClientId(w3_id);
  w3->SetBounds(gfx::Rect(70, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(root_window_id, w3_id));
  ASSERT_TRUE(tree->SetWindowVisibility(w3_id, true));

  // Set capture to |w3|.
  DispatchEventWithoutAck(CreatePointerDownEvent(25, 25));
  ASSERT_TRUE(tree->SetCapture(w3_id));
  EXPECT_EQ(w3, GetCaptureWindow(display));
  AckPreviousEvent();

  // Make |w2| visible. This should not change the capture as the capture is not
  // set to a descendant of the modal parent.
  ASSERT_TRUE(tree->SetWindowVisibility(w2_id, true));
  EXPECT_EQ(w3, GetCaptureWindow(display));
}

// Tests that setting a visible window as modal does not change the capture if
// the capture is not set to a descendant of the modal parent.
TEST_F(WindowTreeTest, VisibleWindowToModalWithNonDescendantCapture) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* w1 = nullptr;
  EXPECT_NO_FATAL_FAILURE(SetupEventTargeting(&embed_connection, &tree, &w1));

  w1->SetBounds(gfx::Rect(10, 10, 30, 30));
  const ServerWindow* root_window = *tree->roots().begin();
  ClientWindowId root_window_id = ClientWindowIdForWindow(tree, root_window);
  ClientWindowId w1_id = ClientWindowIdForWindow(tree, w1);
  Display* display = tree->GetDisplay(w1);

  // Create |w2| and |w3| as children of |root_window| and make them visible.
  ClientWindowId w2_id = BuildClientWindowId(tree, 2);
  ASSERT_TRUE(tree->NewWindow(w2_id, ServerWindow::Properties()));
  ServerWindow* w2 = tree->GetWindowByClientId(w2_id);
  w2->SetBounds(gfx::Rect(50, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(root_window_id, w2_id));
  ASSERT_TRUE(tree->SetWindowVisibility(w2_id, true));

  ClientWindowId w3_id = BuildClientWindowId(tree, 3);
  ASSERT_TRUE(tree->NewWindow(w3_id, ServerWindow::Properties()));
  ServerWindow* w3 = tree->GetWindowByClientId(w3_id);
  w3->SetBounds(gfx::Rect(70, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(root_window_id, w3_id));
  ASSERT_TRUE(tree->SetWindowVisibility(w3_id, true));

  // Set capture to |w3|.
  DispatchEventWithoutAck(CreatePointerDownEvent(25, 25));
  ASSERT_TRUE(tree->SetCapture(w3_id));
  EXPECT_EQ(w3, GetCaptureWindow(display));
  AckPreviousEvent();

  // Set |w2| modal to |w1|. This should not release the capture as the capture
  // is not set to a descendant of the modal parent.
  ASSERT_TRUE(tree->AddTransientWindow(w1_id, w2_id));
  ASSERT_TRUE(tree->SetModal(w2_id));
  EXPECT_EQ(w3, GetCaptureWindow(display));
}

// Tests that moving the capture window to a modal parent releases the capture
// as capture cannot be blocked by a modal window.
TEST_F(WindowTreeTest, MoveCaptureWindowToModalParent) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* w1 = nullptr;
  EXPECT_NO_FATAL_FAILURE(SetupEventTargeting(&embed_connection, &tree, &w1));

  w1->SetBounds(gfx::Rect(10, 10, 30, 30));
  const ServerWindow* root_window = *tree->roots().begin();
  ClientWindowId root_window_id = ClientWindowIdForWindow(tree, root_window);
  ClientWindowId w1_id = ClientWindowIdForWindow(tree, w1);
  Display* display = tree->GetDisplay(w1);

  // Create |w2| and |w3| as children of |root_window| and make them visible.
  ClientWindowId w2_id = BuildClientWindowId(tree, 2);
  ASSERT_TRUE(tree->NewWindow(w2_id, ServerWindow::Properties()));
  ServerWindow* w2 = tree->GetWindowByClientId(w2_id);
  w2->SetBounds(gfx::Rect(50, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(root_window_id, w2_id));
  ASSERT_TRUE(tree->SetWindowVisibility(w2_id, true));

  ClientWindowId w3_id = BuildClientWindowId(tree, 3);
  ASSERT_TRUE(tree->NewWindow(w3_id, ServerWindow::Properties()));
  ServerWindow* w3 = tree->GetWindowByClientId(w3_id);
  w3->SetBounds(gfx::Rect(70, 10, 10, 10));
  ASSERT_TRUE(tree->AddWindow(root_window_id, w3_id));
  ASSERT_TRUE(tree->SetWindowVisibility(w3_id, true));

  // Set |w2| modal to |w1|.
  ASSERT_TRUE(tree->AddTransientWindow(w1_id, w2_id));
  ASSERT_TRUE(tree->SetModal(w2_id));

  // Set capture to |w3|.
  DispatchEventWithoutAck(CreatePointerDownEvent(25, 25));
  ASSERT_TRUE(tree->SetCapture(w3_id));
  EXPECT_EQ(w3, GetCaptureWindow(display));
  AckPreviousEvent();

  // Make |w3| child of |w1|. This should release capture as |w3| is now blocked
  // by a modal window.
  ASSERT_TRUE(tree->AddWindow(w1_id, w3_id));
  EXPECT_EQ(nullptr, GetCaptureWindow(display));
}

// Tests that opacity can be set on a known window.
TEST_F(WindowTreeTest, SetOpacity) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));

  const float new_opacity = 0.5f;
  EXPECT_NE(new_opacity, window->opacity());
  ASSERT_TRUE(tree->SetWindowOpacity(ClientWindowIdForWindow(tree, window),
                                     new_opacity));
  EXPECT_EQ(new_opacity, window->opacity());

  // Re-applying the same opacity will succeed.
  EXPECT_TRUE(tree->SetWindowOpacity(ClientWindowIdForWindow(tree, window),
                                     new_opacity));
}

// Tests that opacity requests for unknown windows are rejected.
TEST_F(WindowTreeTest, SetOpacityFailsOnUnknownWindow) {
  TestWindowTreeClient* embed_connection = nullptr;
  WindowTree* tree = nullptr;
  ServerWindow* window = nullptr;
  EXPECT_NO_FATAL_FAILURE(
      SetupEventTargeting(&embed_connection, &tree, &window));

  TestServerWindowDelegate delegate;
  WindowId window_id(42, 1337);
  ServerWindow unknown_window(&delegate, window_id);
  const float new_opacity = 0.5f;
  ASSERT_NE(new_opacity, unknown_window.opacity());

  EXPECT_FALSE(tree->SetWindowOpacity(
      ClientWindowId(WindowIdToTransportId(window_id)), new_opacity));
  EXPECT_NE(new_opacity, unknown_window.opacity());
}

}  // namespace test
}  // namespace ws
}  // namespace mus
