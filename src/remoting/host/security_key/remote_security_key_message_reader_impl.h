// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_SECURITY_KEY_REMOTE_SECURITY_KEY_MESSAGE_READER_IMPL_H_
#define REMOTING_HOST_SECURITY_KEY_REMOTE_SECURITY_KEY_MESSAGE_READER_IMPL_H_

#include <memory>

#include "base/callback.h"
#include "base/files/file.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread.h"
#include "remoting/host/security_key/remote_security_key_message_reader.h"
#include "remoting/host/security_key/security_key_message.h"

namespace base {
class SingleThreadTaskRunner;
}  // namespace base

namespace remoting {

// RemoteSecurityKeyMessageReader implementation that receives messages from
// a pipe.
class RemoteSecurityKeyMessageReaderImpl
    : public RemoteSecurityKeyMessageReader {
 public:
  explicit RemoteSecurityKeyMessageReaderImpl(base::File input_file);
  ~RemoteSecurityKeyMessageReaderImpl() override;

  // RemoteSecurityKeyMessageReader interface.
  void Start(const SecurityKeyMessageCallback& message_callback,
             const base::Closure& error_callback) override;

 private:
  // Reads a message from the remote security key process and passes it to
  // |message_callback_| on the originating thread. Run on |read_task_runner_|.
  void ReadMessage();

  // Callback run on |read_task_runner_| when an error occurs or EOF is reached.
  void NotifyError();

  // Used for callbacks on the appropriate task runner to signal status changes.
  // These callbacks are invoked on |main_task_runner_|.
  void InvokeMessageCallback(std::unique_ptr<SecurityKeyMessage> message);
  void InvokeErrorCallback();

  base::File read_stream_;

  // Caller-supplied message and error callbacks.
  SecurityKeyMessageCallback message_callback_;
  base::Closure error_callback_;

  // Thread used for blocking IO operations.
  base::Thread reader_thread_;
  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> read_task_runner_;

  base::WeakPtr<RemoteSecurityKeyMessageReaderImpl> reader_;
  base::WeakPtrFactory<RemoteSecurityKeyMessageReaderImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(RemoteSecurityKeyMessageReaderImpl);
};

}  // namespace remoting

#endif  // REMOTING_HOST_SECURITY_KEY_REMOTE_SECURITY_KEY_MESSAGE_READER_IMPL_H_
