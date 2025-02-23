// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A server specific QuicSession subclass.

#ifndef NET_TOOLS_QUIC_QUIC_SERVER_SESSION_BASE_H_
#define NET_TOOLS_QUIC_QUIC_SERVER_SESSION_BASE_H_

#include <stdint.h>

#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "net/quic/crypto/quic_compressed_certs_cache.h"
#include "net/quic/quic_crypto_server_stream.h"
#include "net/quic/quic_protocol.h"
#include "net/quic/quic_spdy_session.h"

namespace net {

class QuicBlockedWriterInterface;
class QuicConfig;
class QuicConnection;
class QuicCryptoServerConfig;
class ReliableQuicStream;


namespace test {
class QuicServerSessionBasePeer;
class QuicSimpleServerSessionPeer;
}  // namespace test

// An interface from the session to the entity owning the session.
// This lets the session notify its owner (the Dispatcher) when the connection
// is closed, blocked, or added/removed from the time-wait list.
class QuicServerSessionVisitor {
 public:
  virtual ~QuicServerSessionVisitor() {}

  virtual void OnConnectionClosed(QuicConnectionId connection_id,
                                  QuicErrorCode error,
                                  const std::string& error_details) = 0;
  virtual void OnWriteBlocked(QuicBlockedWriterInterface* blocked_writer) = 0;
  // Called after the given connection is added to the time-wait list.
  virtual void OnConnectionAddedToTimeWaitList(QuicConnectionId connection_id) {
  }
  // Called after the given connection is removed from the time-wait list.
  virtual void OnConnectionRemovedFromTimeWaitList(
      QuicConnectionId connection_id) {}
};

class QuicServerSessionBase : public QuicSpdySession {
 public:
  // |crypto_config| must outlive the session.
  QuicServerSessionBase(const QuicConfig& config,
                        QuicConnection* connection,
                        QuicServerSessionVisitor* visitor,
                        const QuicCryptoServerConfig* crypto_config,
                        QuicCompressedCertsCache* compressed_certs_cache);

  // Override the base class to notify the owner of the connection close.
  void OnConnectionClosed(QuicErrorCode error,
                          const std::string& error_details,
                          ConnectionCloseSource source) override;
  void OnWriteBlocked() override;

  // Sends a server config update to the client, containing new bandwidth
  // estimate.
  void OnCongestionWindowChange(QuicTime now) override;

  ~QuicServerSessionBase() override;

  void Initialize() override;

  const QuicCryptoServerStreamBase* crypto_stream() const {
    return crypto_stream_.get();
  }

  // Override base class to process bandwidth related config received from
  // client.
  void OnConfigNegotiated() override;

  void set_serving_region(const std::string& serving_region) {
    serving_region_ = serving_region;
  }

  bool server_push_enabled() const { return server_push_enabled_; }

 protected:
  // QuicSession methods(override them with return type of QuicSpdyStream*):
  QuicCryptoServerStreamBase* GetCryptoStream() override;

  // If an outgoing stream can be created, return true.
  // Return false when connection is closed or forward secure encryption hasn't
  // established yet or number of server initiated streams already reaches the
  // upper limit.
  bool ShouldCreateOutgoingDynamicStream() override;

  // If we should create an incoming stream, returns true. Otherwise
  // does error handling, including communicating the error to the client and
  // possibly closing the connection, and returns false.
  bool ShouldCreateIncomingDynamicStream(QuicStreamId id) override;

  virtual QuicCryptoServerStreamBase* CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) = 0;

  const QuicCryptoServerConfig* crypto_config() { return crypto_config_; }

  void set_server_push_enabled(bool enable) { server_push_enabled_ = enable; }

 private:
  friend class test::QuicServerSessionBasePeer;
  friend class test::QuicSimpleServerSessionPeer;

  const QuicCryptoServerConfig* crypto_config_;

  // The cache which contains most recently compressed certs.
  // Owned by QuicDispatcher.
  QuicCompressedCertsCache* compressed_certs_cache_;

  scoped_ptr<QuicCryptoServerStreamBase> crypto_stream_;
  QuicServerSessionVisitor* visitor_;

  // Whether bandwidth resumption is enabled for this connection.
  bool bandwidth_resumption_enabled_;

  // The most recent bandwidth estimate sent to the client.
  QuicBandwidth bandwidth_estimate_sent_to_client_;

  // Text describing server location. Sent to the client as part of the bandwith
  // estimate in the source-address token. Optional, can be left empty.
  std::string serving_region_;

  // Time at which we send the last SCUP to the client.
  QuicTime last_scup_time_;

  // Number of packets sent to the peer, at the time we last sent a SCUP.
  int64_t last_scup_packet_number_;

  // Converts QuicBandwidth to an int32 bytes/second that can be
  // stored in CachedNetworkParameters.  TODO(jokulik): This function
  // should go away once we fix http://b//27897982
  int32_t BandwidthToCachedParameterBytesPerSecond(
      const QuicBandwidth& bandwidth);

  // Set during handshake. If true, resources in x-associated-content and link
  // headers will be pushed. see: go/gfe_server_push.
  bool server_push_enabled_;

  DISALLOW_COPY_AND_ASSIGN(QuicServerSessionBase);
};

}  // namespace net

#endif  // NET_TOOLS_QUIC_QUIC_SERVER_SESSION_BASE_H_
