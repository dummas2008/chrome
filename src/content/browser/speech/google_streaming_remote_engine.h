// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SPEECH_GOOGLE_STREAMING_REMOTE_ENGINE_H_
#define CONTENT_BROWSER_SPEECH_GOOGLE_STREAMING_REMOTE_ENGINE_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/threading/non_thread_safe.h"
#include "content/browser/speech/audio_encoder.h"
#include "content/browser/speech/chunked_byte_buffer.h"
#include "content/browser/speech/speech_recognition_engine.h"
#include "content/common/content_export.h"
#include "content/public/common/speech_recognition_error.h"
#include "net/url_request/url_fetcher_delegate.h"

namespace net {
class URLRequestContextGetter;
}

namespace content {

class AudioChunk;
struct SpeechRecognitionError;
struct SpeechRecognitionResult;

// Implements a SpeechRecognitionEngine supporting continuous recognition by
// means of interaction with Google streaming speech recognition webservice.
// More in details, this class establishes two HTTP(S) connections with the
// webservice, for each session, herein called "upstream" and "downstream".
// Audio chunks are sent on the upstream by means of a chunked HTTP POST upload.
// Recognition results are retrieved in a full-duplex fashion (i.e. while
// pushing audio on the upstream) on the downstream by means of a chunked
// HTTP GET request. Pairing between the two stream is handled through a
// randomly generated key, unique for each request, which is passed in the
// &pair= arg to both stream request URLs.
// In the case of a regular session, the upstream is closed when the audio
// capture ends (notified through a |AudioChunksEnded| call) and the downstream
// waits for a corresponding server closure (eventually some late results can
// come after closing the upstream).
// Both stream are guaranteed to be closed when |EndRecognition| call is issued.
class CONTENT_EXPORT GoogleStreamingRemoteEngine
    : public NON_EXPORTED_BASE(SpeechRecognitionEngine),
      public net::URLFetcherDelegate,
      public NON_EXPORTED_BASE(base::NonThreadSafe) {
 public:
  // Duration of each audio packet.
  static const int kAudioPacketIntervalMs;

  // IDs passed to URLFetcher::Create(). Used for testing.
  static const int kUpstreamUrlFetcherIdForTesting;
  static const int kDownstreamUrlFetcherIdForTesting;

  explicit GoogleStreamingRemoteEngine(net::URLRequestContextGetter* context);
  ~GoogleStreamingRemoteEngine() override;

  // SpeechRecognitionEngine methods.
  void SetConfig(const SpeechRecognitionEngineConfig& config) override;
  void StartRecognition() override;
  void EndRecognition() override;
  void TakeAudioChunk(const AudioChunk& data) override;
  void AudioChunksEnded() override;
  bool IsRecognitionPending() const override;
  int GetDesiredAudioChunkDurationMs() const override;

  // net::URLFetcherDelegate methods.
  void OnURLFetchComplete(const net::URLFetcher* source) override;
  void OnURLFetchDownloadProgress(const net::URLFetcher* source,
                                  int64_t current,
                                  int64_t total) override;

 private:
  // Response status codes from the speech recognition webservice.
  static const int kWebserviceStatusNoError;
  static const int kWebserviceStatusErrorNoMatch;

  // Frame type for framed POST data. Do NOT change these. They must match
  // values the server expects.
  enum FrameType {
    FRAME_PREAMBLE_AUDIO = 0,
    FRAME_RECOGNITION_AUDIO = 1
  };

  // Data types for the internal Finite State Machine (FSM).
  enum FSMState {
    STATE_IDLE = 0,
    STATE_BOTH_STREAMS_CONNECTED,
    STATE_WAITING_DOWNSTREAM_RESULTS,
    STATE_MAX_VALUE = STATE_WAITING_DOWNSTREAM_RESULTS
  };

  enum FSMEvent {
    EVENT_END_RECOGNITION = 0,
    EVENT_START_RECOGNITION,
    EVENT_AUDIO_CHUNK,
    EVENT_AUDIO_CHUNKS_ENDED,
    EVENT_UPSTREAM_ERROR,
    EVENT_DOWNSTREAM_ERROR,
    EVENT_DOWNSTREAM_RESPONSE,
    EVENT_DOWNSTREAM_CLOSED,
    EVENT_MAX_VALUE = EVENT_DOWNSTREAM_CLOSED
  };

  struct FSMEventArgs {
    explicit FSMEventArgs(FSMEvent event_value);
    ~FSMEventArgs();

    FSMEvent event;

    // In case of EVENT_AUDIO_CHUNK, holds the chunk pushed by |TakeAudioChunk|.
    scoped_refptr<const AudioChunk> audio_data;

    // In case of EVENT_DOWNSTREAM_RESPONSE, hold the current chunk bytes.
    std::unique_ptr<std::vector<uint8_t>> response;

   private:
    DISALLOW_COPY_AND_ASSIGN(FSMEventArgs);
  };

  // Invoked by both upstream and downstream URLFetcher callbacks to handle
  // new chunk data, connection closed or errors notifications.
  void DispatchHTTPResponse(const net::URLFetcher* source,
                            bool end_of_response);

  // Entry point for pushing any new external event into the recognizer FSM.
  void DispatchEvent(const FSMEventArgs& event_args);

  // Defines the behavior of the recognizer FSM, selecting the appropriate
  // transition according to the current state and event.
  FSMState ExecuteTransitionAndGetNextState(const FSMEventArgs& event_args);

  // The methods below handle transitions of the recognizer FSM.
  FSMState ConnectBothStreams(const FSMEventArgs& event_args);
  FSMState TransmitAudioUpstream(const FSMEventArgs& event_args);
  FSMState ProcessDownstreamResponse(const FSMEventArgs& event_args);
  FSMState RaiseNoMatchErrorIfGotNoResults(const FSMEventArgs& event_args);
  FSMState CloseUpstreamAndWaitForResults(const FSMEventArgs& event_args);
  FSMState CloseDownstream(const FSMEventArgs& event_args);
  FSMState AbortSilently(const FSMEventArgs& event_args);
  FSMState AbortWithError(const FSMEventArgs& event_args);
  FSMState Abort(SpeechRecognitionErrorCode error);
  FSMState DoNothing(const FSMEventArgs& event_args);
  FSMState NotFeasible(const FSMEventArgs& event_args);

  std::string GetAcceptedLanguages() const;
  std::string GenerateRequestKey() const;

  // Upload a single chunk of audio data. Handles both unframed and framed
  // upload formats, and uses the appropriate one.
  void UploadAudioChunk(const std::string& data, FrameType type, bool is_final);

  SpeechRecognitionEngineConfig config_;
  std::unique_ptr<net::URLFetcher> upstream_fetcher_;
  std::unique_ptr<net::URLFetcher> downstream_fetcher_;
  scoped_refptr<net::URLRequestContextGetter> url_context_;
  std::unique_ptr<AudioEncoder> encoder_;
  std::unique_ptr<AudioEncoder> preamble_encoder_;
  ChunkedByteBuffer chunked_byte_buffer_;
  size_t previous_response_length_;
  bool got_last_definitive_result_;
  bool is_dispatching_event_;
  bool use_framed_post_data_;
  FSMState state_;

  DISALLOW_COPY_AND_ASSIGN(GoogleStreamingRemoteEngine);
};

}  // namespace content

#endif  // CONTENT_BROWSER_SPEECH_GOOGLE_STREAMING_REMOTE_ENGINE_H_
