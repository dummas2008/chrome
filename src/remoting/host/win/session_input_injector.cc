// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/win/session_input_injector.h"

#include <stddef.h>

#include <set>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/single_thread_task_runner.h"
#include "base/win/windows_version.h"
#include "remoting/host/sas_injector.h"
#include "remoting/proto/event.pb.h"
#include "third_party/webrtc/modules/desktop_capture/win/desktop.h"
#include "third_party/webrtc/modules/desktop_capture/win/scoped_thread_desktop.h"
#include "ui/events/keycodes/dom/dom_code.h"

namespace {

bool CheckCtrlAndAltArePressed(const std::set<ui::DomCode>& pressed_keys) {
  size_t ctrl_keys = pressed_keys.count(ui::DomCode::CONTROL_LEFT) +
                     pressed_keys.count(ui::DomCode::CONTROL_RIGHT);
  size_t alt_keys = pressed_keys.count(ui::DomCode::ALT_LEFT) +
                    pressed_keys.count(ui::DomCode::ALT_RIGHT);
  return ctrl_keys != 0 && alt_keys != 0 &&
    (ctrl_keys + alt_keys == pressed_keys.size());
}

} // namespace

namespace remoting {

using protocol::ClipboardEvent;
using protocol::KeyEvent;
using protocol::MouseEvent;
using protocol::TextEvent;
using protocol::TouchEvent;

class SessionInputInjectorWin::Core
    : public base::RefCountedThreadSafe<SessionInputInjectorWin::Core>,
      public InputInjector {
 public:
  Core(scoped_refptr<base::SingleThreadTaskRunner> input_task_runner,
       std::unique_ptr<InputInjector> nested_executor,
       scoped_refptr<base::SingleThreadTaskRunner> inject_sas_task_runner,
       const base::Closure& inject_sas);

  // InputInjector implementation.
  void Start(std::unique_ptr<ClipboardStub> client_clipboard) override;

  // protocol::ClipboardStub implementation.
  void InjectClipboardEvent(const ClipboardEvent& event) override;

  // protocol::InputStub implementation.
  void InjectKeyEvent(const KeyEvent& event) override;
  void InjectTextEvent(const TextEvent& event) override;
  void InjectMouseEvent(const MouseEvent& event) override;
  void InjectTouchEvent(const TouchEvent& event) override;

 private:
  friend class base::RefCountedThreadSafe<Core>;
  ~Core() override;

  // Switches to the desktop receiving a user input if different from
  // the current one.
  void SwitchToInputDesktop();

  scoped_refptr<base::SingleThreadTaskRunner> input_task_runner_;

  // Pointer to the next event executor.
  std::unique_ptr<InputInjector> nested_executor_;

  scoped_refptr<base::SingleThreadTaskRunner> inject_sas_task_runner_;

  webrtc::ScopedThreadDesktop desktop_;

  // Used to inject Secure Attention Sequence on Vista+.
  base::Closure inject_sas_;

  // Used to inject Secure Attention Sequence on XP.
  std::unique_ptr<SasInjector> sas_injector_;

  // Keys currently pressed by the client, used to detect Ctrl-Alt-Del.
  std::set<ui::DomCode> pressed_keys_;

  DISALLOW_COPY_AND_ASSIGN(Core);
};

SessionInputInjectorWin::Core::Core(
    scoped_refptr<base::SingleThreadTaskRunner> input_task_runner,
    std::unique_ptr<InputInjector> nested_executor,
    scoped_refptr<base::SingleThreadTaskRunner> inject_sas_task_runner,
    const base::Closure& inject_sas)
    : input_task_runner_(input_task_runner),
      nested_executor_(std::move(nested_executor)),
      inject_sas_task_runner_(inject_sas_task_runner),
      inject_sas_(inject_sas) {}

void SessionInputInjectorWin::Core::Start(
    std::unique_ptr<protocol::ClipboardStub> client_clipboard) {
  if (!input_task_runner_->BelongsToCurrentThread()) {
    input_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&Core::Start, this, base::Passed(&client_clipboard)));
    return;
  }

  nested_executor_->Start(std::move(client_clipboard));
}

void SessionInputInjectorWin::Core::InjectClipboardEvent(
    const ClipboardEvent& event) {
  if (!input_task_runner_->BelongsToCurrentThread()) {
    input_task_runner_->PostTask(
        FROM_HERE, base::Bind(&Core::InjectClipboardEvent, this, event));
    return;
  }

  nested_executor_->InjectClipboardEvent(event);
}

void SessionInputInjectorWin::Core::InjectKeyEvent(const KeyEvent& event) {
  if (!input_task_runner_->BelongsToCurrentThread()) {
    input_task_runner_->PostTask(
        FROM_HERE, base::Bind(&Core::InjectKeyEvent, this, event));
    return;
  }

  // HostEventDispatcher should drop events lacking the pressed field.
  DCHECK(event.has_pressed());

  if (event.has_usb_keycode()) {
    ui::DomCode dom_code = static_cast<ui::DomCode>(event.usb_keycode());
    if (event.pressed()) {
      // Simulate secure attention sequence if Ctrl-Alt-Del was just pressed.
      if (dom_code == ui::DomCode::DEL &&
          CheckCtrlAndAltArePressed(pressed_keys_)) {
        VLOG(3) << "Sending Secure Attention Sequence to the session";

        if (base::win::GetVersion() < base::win::VERSION_VISTA) {
          if (!sas_injector_)
            sas_injector_ = SasInjector::Create();
          if (!sas_injector_->InjectSas())
            LOG(ERROR) << "Failed to inject Secure Attention Sequence.";
        } else {
          inject_sas_task_runner_->PostTask(FROM_HERE, inject_sas_);
        }
      }

      pressed_keys_.insert(dom_code);
    } else {
      pressed_keys_.erase(dom_code);
    }
  }

  SwitchToInputDesktop();
  nested_executor_->InjectKeyEvent(event);
}

void SessionInputInjectorWin::Core::InjectTextEvent(const TextEvent& event) {
  if (!input_task_runner_->BelongsToCurrentThread()) {
    input_task_runner_->PostTask(
        FROM_HERE, base::Bind(&Core::InjectTextEvent, this, event));
    return;
  }

  SwitchToInputDesktop();
  nested_executor_->InjectTextEvent(event);
}

void SessionInputInjectorWin::Core::InjectMouseEvent(const MouseEvent& event) {
  if (!input_task_runner_->BelongsToCurrentThread()) {
    input_task_runner_->PostTask(
        FROM_HERE, base::Bind(&Core::InjectMouseEvent, this, event));
    return;
  }

  SwitchToInputDesktop();
  nested_executor_->InjectMouseEvent(event);
}

void SessionInputInjectorWin::Core::InjectTouchEvent(const TouchEvent& event) {
  if (!input_task_runner_->BelongsToCurrentThread()) {
    input_task_runner_->PostTask(
        FROM_HERE, base::Bind(&Core::InjectTouchEvent, this, event));
    return;
  }

  SwitchToInputDesktop();
  nested_executor_->InjectTouchEvent(event);
}

SessionInputInjectorWin::Core::~Core() {
}

void SessionInputInjectorWin::Core::SwitchToInputDesktop() {
  // Switch to the desktop receiving user input if different from the current
  // one.
  std::unique_ptr<webrtc::Desktop> input_desktop(
      webrtc::Desktop::GetInputDesktop());
  if (input_desktop.get() != nullptr && !desktop_.IsSame(*input_desktop)) {
    // If SetThreadDesktop() fails, the thread is still assigned a desktop.
    // So we can continue capture screen bits, just from a diffected desktop.
    desktop_.SetThreadDesktop(input_desktop.release());
  }
}

SessionInputInjectorWin::SessionInputInjectorWin(
    scoped_refptr<base::SingleThreadTaskRunner> input_task_runner,
    std::unique_ptr<InputInjector> nested_executor,
    scoped_refptr<base::SingleThreadTaskRunner> inject_sas_task_runner,
    const base::Closure& inject_sas) {
  core_ = new Core(input_task_runner, std::move(nested_executor),
                   inject_sas_task_runner, inject_sas);
}

SessionInputInjectorWin::~SessionInputInjectorWin() {
}

void SessionInputInjectorWin::Start(
    std::unique_ptr<protocol::ClipboardStub> client_clipboard) {
  core_->Start(std::move(client_clipboard));
}

void SessionInputInjectorWin::InjectClipboardEvent(
    const protocol::ClipboardEvent& event) {
  core_->InjectClipboardEvent(event);
}

void SessionInputInjectorWin::InjectKeyEvent(const protocol::KeyEvent& event) {
  core_->InjectKeyEvent(event);
}

void SessionInputInjectorWin::InjectTextEvent(
    const protocol::TextEvent& event) {
  core_->InjectTextEvent(event);
}

void SessionInputInjectorWin::InjectMouseEvent(
    const protocol::MouseEvent& event) {
  core_->InjectMouseEvent(event);
}

void SessionInputInjectorWin::InjectTouchEvent(
    const protocol::TouchEvent& event) {
  core_->InjectTouchEvent(event);
}

}  // namespace remoting
