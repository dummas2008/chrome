// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_CHANNEL_H_
#define MOJO_EDK_SYSTEM_CHANNEL_H_

#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/process/process_handle.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/platform_handle_vector.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"

namespace mojo {
namespace edk {

const size_t kChannelMessageAlignment = 8;

// Channel provides a thread-safe interface to read and write arbitrary
// delimited messages over an underlying I/O channel, optionally transferring
// one or more platform handles in the process.
class Channel : public base::RefCountedThreadSafe<Channel> {
 public:
  struct Message;

  using MessagePtr = scoped_ptr<Message>;

  // A message to be written to a channel.
  struct Message {
#pragma pack(push, 1)
    struct Header {
      enum class MessageType : uint16_t {
        // A normal message.
        NORMAL = 0,
#if defined(OS_MACOSX)
        // A control message containing handles to echo back.
        HANDLES_SENT,
        // A control message containing handles that can now be closed.
        HANDLES_SENT_ACK,
#endif
      };

      // Message size in bytes, including the header.
      uint32_t num_bytes;

#if defined(OS_CHROMEOS) || defined(OS_ANDROID)
      // Old message wire format for ChromeOS and Android.
      // Number of attached handles.
      uint16_t num_handles;

      MessageType message_type;
#else
      // Total size of header, including extra header data (i.e. HANDLEs on
      // windows).
      uint16_t num_header_bytes;

      // Number of attached handles. May be less than the reserved handle
      // storage size in this message on platforms that serialise handles as
      // data (i.e. HANDLEs on Windows, Mach ports on OSX).
      uint16_t num_handles;

      MessageType message_type;

      char padding[6];
#endif  // defined(OS_CHROMEOS) || defined(OS_ANDROID)
    };

#if defined(OS_MACOSX) && !defined(OS_IOS)
    struct MachPortsEntry {
      uint16_t index;
      uint32_t mach_port;
      static_assert(sizeof(mach_port_t) <= sizeof(uint32_t),
                    "mach_port_t must be no larger than uint32_t");
    };
    static_assert(sizeof(MachPortsEntry) == 6,
                  "sizeof(MachPortsEntry) must be 6 bytes");
#endif
#pragma pack(pop)

    // Allocates and owns a buffer for message data with enough capacity for
    // |payload_size| bytes plus a header, plus |max_handles| platform handles.
    Message(size_t payload_size,
            size_t max_handles,
            Header::MessageType message_type = Header::MessageType::NORMAL);

    ~Message();

    // Constructs a Message from serialized message data.
    static MessagePtr Deserialize(const void* data, size_t data_num_bytes);

    const void* data() const { return data_; }
    size_t data_num_bytes() const { return size_; }

#if defined(OS_CHROMEOS) || defined(OS_ANDROID)
    void* mutable_payload() { return static_cast<void*>(header_ + 1); }
    const void* payload() const {
      return static_cast<const void*>(header_ + 1);
    }
    size_t payload_size() const;
#else
    const void* extra_header() const { return data_ + sizeof(Header); }
    void* mutable_extra_header() { return data_ + sizeof(Header); }
    size_t extra_header_size() const {
      return header_->num_header_bytes - sizeof(Header);
    }

    void* mutable_payload() { return data_ + header_->num_header_bytes; }
    const void* payload() const { return data_ + header_->num_header_bytes; }
    size_t payload_size() const;
#endif  // defined(OS_CHROMEOS) || defined(OS_ANDROID)

    size_t num_handles() const { return header_->num_handles; }
    bool has_handles() const { return header_->num_handles > 0; }
    PlatformHandle* handles();
#if defined(OS_MACOSX) && !defined(OS_IOS)
    bool has_mach_ports() const;
#endif

    // Note: SetHandles() and TakeHandles() invalidate any previous value of
    // handles().
    void SetHandles(ScopedPlatformHandleVectorPtr new_handles);
    ScopedPlatformHandleVectorPtr TakeHandles();
    // Version of TakeHandles that returns a vector of platform handles suitable
    // for transfer over an underlying OS mechanism. i.e. file descriptors over
    // a unix domain socket. Any handle that cannot be transferred this way,
    // such as Mach ports, will be removed.
    ScopedPlatformHandleVectorPtr TakeHandlesForTransport();

#if defined(OS_WIN)
    // Prepares the handles in this message for use in a different process.
    // Upon calling this the handles should belong to |from_process|; after the
    // call they'll belong to |to_process|. The source handles are always
    // closed by this call. Returns false iff one or more handles failed
    // duplication.
    static bool RewriteHandles(base::ProcessHandle from_process,
                               base::ProcessHandle to_process,
                               PlatformHandle* handles,
                               size_t num_handles);
#endif

   private:
    size_t size_;
    size_t max_handles_;
    char* data_;
    Header* header_;

#if defined(OS_WIN)
    // On Windows, handles are serialised into the extra header section.
    PlatformHandle* handles_ = nullptr;
#else
    ScopedPlatformHandleVectorPtr handle_vector_;
#endif

#if defined(OS_MACOSX) && !defined(OS_IOS)
    // On OSX, handles are serialised into the extra header section.
    MachPortsEntry* mach_ports_ = nullptr;
#endif

    DISALLOW_COPY_AND_ASSIGN(Message);
  };

  // Delegate methods are called from the I/O task runner with which the Channel
  // was created (see Channel::Create).
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Notify of a received message. |payload| is not owned and must not be
    // retained; it will be null if |payload_size| is 0. |handles| are
    // transferred to the callee.
    virtual void OnChannelMessage(const void* payload,
                                  size_t payload_size,
                                  ScopedPlatformHandleVectorPtr handles) = 0;

    // Notify that an error has occured and the Channel will cease operation.
    virtual void OnChannelError() = 0;
  };

  // Creates a new Channel around a |platform_handle|, taking ownership of the
  // handle. All I/O on the handle will be performed on |io_task_runner|.
  // Note that ShutDown() MUST be called on the Channel some time before
  // |delegate| is destroyed.
  static scoped_refptr<Channel> Create(
      Delegate* delegate,
      ScopedPlatformHandle platform_handle,
      scoped_refptr<base::TaskRunner> io_task_runner);

  // Request that the channel be shut down. This should always be called before
  // releasing the last reference to a Channel to ensure that it's cleaned up
  // on its I/O task runner's thread.
  //
  // Delegate methods will no longer be invoked after this call.
  void ShutDown();

  // Begin processing I/O events. Delegate methods must only be invoked after
  // this call.
  virtual void Start() = 0;

  // Stop processing I/O events.
  virtual void ShutDownImpl() = 0;

  // Queues an outgoing message on the Channel. This message will either
  // eventually be written or will fail to write and trigger
  // Delegate::OnChannelError.
  virtual void Write(MessagePtr message) = 0;

 protected:
  explicit Channel(Delegate* delegate);
  virtual ~Channel();

  // Called by the implementation when it wants somewhere to stick data.
  // |*buffer_capacity| may be set by the caller to indicate the desired buffer
  // size. If 0, a sane default size will be used instead.
  //
  // Returns the address of a buffer which can be written to, and indicates its
  // actual capacity in |*buffer_capacity|.
  char* GetReadBuffer(size_t* buffer_capacity);

  // Called by the implementation when new data is available in the read
  // buffer. Returns false to indicate an error. Upon success,
  // |*next_read_size_hint| will be set to a recommended size for the next
  // read done by the implementation.
  bool OnReadComplete(size_t bytes_read, size_t* next_read_size_hint);

  // Called by the implementation when something goes horribly wrong. It is NOT
  // OK to call this synchronously from any public interface methods.
  void OnError();

  // Retrieves the set of platform handles read for a given message.
  // |extra_header| and |extra_header_size| correspond to the extra header data.
  // Depending on the Channel implementation, this body may encode platform
  // handles, or handles may be stored and managed elsewhere by the
  // implementation.  If |num_handles| handles cannot be returned, this must
  // return null.
  virtual ScopedPlatformHandleVectorPtr GetReadPlatformHandles(
      size_t num_handles,
      const void* extra_header,
      size_t extra_header_size) = 0;

  virtual void OnControlMessage(Message::Header::MessageType message_type,
                                const void* payload,
                                size_t payload_size,
                                ScopedPlatformHandleVectorPtr handles) {}

 private:
  friend class base::RefCountedThreadSafe<Channel>;

  class ReadBuffer;

  Delegate* delegate_;
  const scoped_ptr<ReadBuffer> read_buffer_;

  DISALLOW_COPY_AND_ASSIGN(Channel);
};

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_CHANNEL_H_
