// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLIMP_CLIENT_FEATURE_IME_FEATURE_H_
#define BLIMP_CLIENT_FEATURE_IME_FEATURE_H_

#include <map>
#include <string>

#include "base/macros.h"
#include "blimp/client/blimp_client_export.h"
#include "blimp/net/blimp_message_processor.h"
#include "ui/base/ime/text_input_type.h"

namespace blimp {
namespace client {

// Handles all incoming and outgoing protobuf messages for text input of type
// BlimpMessage::IME for blimp client.
// Upon receiving a text input request from the engine, the ImeFeature caches
// the |tab_id_| and |render_widget_id_| for the request and
// delegates the request to the Delegate which then opens up the IME.
// After user is done typing, the text is passed back to ImeFeature, which then
// sends the text to the engine over network along with the same |tab_id_| and
// |render_widget_id_|.
// Any time user taps on an input text, ImeMessage::SHOW_IME message will be
// sent to client. Similarly, any time the text input is out of focus (e.g. if
// user navigates away from the currently page or the page loads for the first
// time), ImeMessage::HIDE_IME will be sent.

class BLIMP_CLIENT_EXPORT ImeFeature : public BlimpMessageProcessor {
 public:
  // A delegate to be notified of text input requests.
  class Delegate {
   public:
    virtual void OnShowImeRequested(ui::TextInputType input_type,
                                    const std::string& text) = 0;
    virtual void OnHideImeRequested() = 0;
  };

  ImeFeature();
  ~ImeFeature() override;

  // Set the BlimpMessageProcessor that will be used to send BlimpMessage::IME
  // messages to the engine.
  void set_outgoing_message_processor(
      std::unique_ptr<BlimpMessageProcessor> processor) {
    outgoing_message_processor_ = std::move(processor);
  }

  // Sets a Delegate to be notified of all text input messages.
  // Passing a null |delegate| causes IME messages to be ignored.
  void set_delegate(Delegate* delegate) { delegate_ = delegate; }

  // Sends text from IME to the blimp engine.
  void OnImeTextEntered(const std::string& text);

 private:
  // BlimpMessageProcessor implementation.
  void ProcessMessage(std::unique_ptr<BlimpMessage> message,
                      const net::CompletionCallback& callback) override;

  // Delegate for processing the text input related messages. |delegate_| must
  // remain valid until the last message has been passed to ImeFeature.
  Delegate* delegate_ = nullptr;

  // Tab id and render widget id for the input field for which user input is
  // being requested.
  // The values are cached from the ImeMessage::SHOW_IME message and sent back
  // to engine in the subsequent ImeMessage::SET_TEXT message.
  // The cached values are cleared on receiving ImeMessage::HIDE_IME request.
  int tab_id_ = -1;
  int render_widget_id_ = 0;

  // Used to send BlimpMessage::IME messages to the engine.
  std::unique_ptr<BlimpMessageProcessor> outgoing_message_processor_;

  DISALLOW_COPY_AND_ASSIGN(ImeFeature);
};

}  // namespace client
}  // namespace blimp

#endif  // BLIMP_CLIENT_FEATURE_IME_FEATURE_H_
