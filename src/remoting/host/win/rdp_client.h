// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_WIN_RDP_CLIENT_H_
#define REMOTING_HOST_WIN_RDP_CLIENT_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/threading/non_thread_safe.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace webrtc {
class DesktopSize;
}  // namespace webrtc

namespace remoting {

// Establishes a loopback RDP connection to spawn a new Windows session.
class RdpClient : public base::NonThreadSafe {
 public:
  class EventHandler {
   public:
    virtual ~EventHandler() {}

    // Notifies the event handler that an RDP connection has been established
    // successfully.
    virtual void OnRdpConnected() = 0;

    // Notifies that the RDP connection has been closed.
    virtual void OnRdpClosed() = 0;
  };

  RdpClient(
      scoped_refptr<base::SingleThreadTaskRunner> caller_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner,
      const webrtc::DesktopSize& screen_size,
      const std::string& terminal_id,
      EventHandler* event_handler);
  virtual ~RdpClient();

  // Sends Secure Attention Sequence to the session.
  void InjectSas();

 private:
  // The actual implementation resides in Core class.
  class Core;
  scoped_refptr<Core> core_;

  DISALLOW_COPY_AND_ASSIGN(RdpClient);
};

}  // namespace remoting

#endif  // REMOTING_HOST_WIN_RDP_CLIENT_H_
