// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_APPCACHE_APPCACHE_RESPONSE_H_
#define CONTENT_BROWSER_APPCACHE_APPCACHE_RESPONSE_H_

#include <stdint.h>

#include <memory>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "content/common/appcache_interfaces.h"
#include "content/common/content_export.h"
#include "net/base/completion_callback.h"
#include "net/http/http_response_info.h"
#include "url/gurl.h"

namespace net {
class IOBuffer;
}

namespace content {
class AppCacheStorage;
class MockAppCacheStorage;

static const int kUnkownResponseDataSize = -1;

// Response info for a particular response id. Instances are tracked in
// the working set.
class CONTENT_EXPORT AppCacheResponseInfo
    : public base::RefCounted<AppCacheResponseInfo> {
 public:
  // AppCacheResponseInfo takes ownership of the http_info.
  AppCacheResponseInfo(AppCacheStorage* storage,
                       const GURL& manifest_url,
                       int64_t response_id,
                       net::HttpResponseInfo* http_info,
                       int64_t response_data_size);

  const GURL& manifest_url() const { return manifest_url_; }
  int64_t response_id() const { return response_id_; }
  const net::HttpResponseInfo* http_response_info() const {
    return http_response_info_.get();
  }
  int64_t response_data_size() const { return response_data_size_; }

 private:
  friend class base::RefCounted<AppCacheResponseInfo>;
  virtual ~AppCacheResponseInfo();

  const GURL manifest_url_;
  const int64_t response_id_;
  const std::unique_ptr<net::HttpResponseInfo> http_response_info_;
  const int64_t response_data_size_;
  AppCacheStorage* storage_;
};

// A refcounted wrapper for HttpResponseInfo so we can apply the
// refcounting semantics used with IOBuffer with these structures too.
struct CONTENT_EXPORT HttpResponseInfoIOBuffer
    : public base::RefCountedThreadSafe<HttpResponseInfoIOBuffer> {
  std::unique_ptr<net::HttpResponseInfo> http_info;
  int response_data_size;

  HttpResponseInfoIOBuffer();
  explicit HttpResponseInfoIOBuffer(net::HttpResponseInfo* info);

 protected:
  friend class base::RefCountedThreadSafe<HttpResponseInfoIOBuffer>;
  virtual ~HttpResponseInfoIOBuffer();
};

// Low level storage API used by the response reader and writer.
class CONTENT_EXPORT AppCacheDiskCacheInterface {
 public:
  class Entry {
   public:
    virtual int Read(int index,
                     int64_t offset,
                     net::IOBuffer* buf,
                     int buf_len,
                     const net::CompletionCallback& callback) = 0;
    virtual int Write(int index,
                      int64_t offset,
                      net::IOBuffer* buf,
                      int buf_len,
                      const net::CompletionCallback& callback) = 0;
    virtual int64_t GetSize(int index) = 0;
    virtual void Close() = 0;
   protected:
    virtual ~Entry() {}
  };

  AppCacheDiskCacheInterface();

  virtual int CreateEntry(int64_t key,
                          Entry** entry,
                          const net::CompletionCallback& callback) = 0;
  virtual int OpenEntry(int64_t key,
                        Entry** entry,
                        const net::CompletionCallback& callback) = 0;
  virtual int DoomEntry(int64_t key,
                        const net::CompletionCallback& callback) = 0;

  base::WeakPtr<AppCacheDiskCacheInterface> GetWeakPtr();

 protected:
  virtual ~AppCacheDiskCacheInterface();

  base::WeakPtrFactory<AppCacheDiskCacheInterface> weak_factory_;
};

// Common base class for response reader and writer.
class CONTENT_EXPORT AppCacheResponseIO {
 public:
  virtual ~AppCacheResponseIO();
  int64_t response_id() const { return response_id_; }

 protected:
  AppCacheResponseIO(
      int64_t response_id,
      int64_t group_id,
      const base::WeakPtr<AppCacheDiskCacheInterface>& disk_cache);

  virtual void OnIOComplete(int result) = 0;
  virtual void OnOpenEntryComplete() {}

  bool IsIOPending() { return !callback_.is_null(); }
  void ScheduleIOCompletionCallback(int result);
  void InvokeUserCompletionCallback(int result);
  void ReadRaw(int index, int offset, net::IOBuffer* buf, int buf_len);
  void WriteRaw(int index, int offset, net::IOBuffer* buf, int buf_len);
  void OpenEntryIfNeeded();

  const int64_t response_id_;
  const int64_t group_id_;
  base::WeakPtr<AppCacheDiskCacheInterface> disk_cache_;
  AppCacheDiskCacheInterface::Entry* entry_;
  scoped_refptr<HttpResponseInfoIOBuffer> info_buffer_;
  scoped_refptr<net::IOBuffer> buffer_;
  int buffer_len_;
  net::CompletionCallback callback_;
  net::CompletionCallback open_callback_;
  base::WeakPtrFactory<AppCacheResponseIO> weak_factory_;

 private:
  void OnRawIOComplete(int result);
  void OpenEntryCallback(AppCacheDiskCacheInterface::Entry** entry, int rv);
};

// Reads existing response data from storage. If the object is deleted
// and there is a read in progress, the implementation will return
// immediately but will take care of any side effect of cancelling the
// operation.  In other words, instances are safe to delete at will.
class CONTENT_EXPORT AppCacheResponseReader
    : public AppCacheResponseIO {
 public:
  ~AppCacheResponseReader() override;

  // Reads http info from storage. Always returns the result of the read
  // asynchronously through the 'callback'. Returns the number of bytes read
  // or a net:: error code. Guaranteed to not perform partial reads of
  // the info data. The reader acquires a reference to the 'info_buf' until
  // completion at which time the callback is invoked with either a negative
  // error code or the number of bytes read. The 'info_buf' argument should
  // contain a NULL http_info when ReadInfo is called. The 'callback' is a
  // required parameter.
  // Should only be called where there is no Read operation in progress.
  // (virtual for testing)
  virtual void ReadInfo(HttpResponseInfoIOBuffer* info_buf,
                        const net::CompletionCallback& callback);

  // Reads data from storage. Always returns the result of the read
  // asynchronously through the 'callback'. Returns the number of bytes read
  // or a net:: error code. EOF is indicated with a return value of zero.
  // The reader acquires a reference to the provided 'buf' until completion
  // at which time the callback is invoked with either a negative error code
  // or the number of bytes read. The 'callback' is a required parameter.
  // Should only be called where there is no Read operation in progress.
  // (virtual for testing)
  virtual void ReadData(net::IOBuffer* buf, int buf_len,
                        const net::CompletionCallback& callback);

  // Returns true if there is a read operation, for data or info, pending.
  bool IsReadPending() { return IsIOPending(); }

  // Used to support range requests. If not called, the reader will
  // read the entire response body. If called, this must be called prior
  // to the first call to the ReadData method.
  void SetReadRange(int offset, int length);

 protected:
  friend class AppCacheStorageImpl;
  friend class content::MockAppCacheStorage;

  // Should only be constructed by the storage class and derivatives.
  AppCacheResponseReader(
      int64_t response_id,
      int64_t group_id,
      const base::WeakPtr<AppCacheDiskCacheInterface>& disk_cache);

  void OnIOComplete(int result) override;
  void OnOpenEntryComplete() override;
  void ContinueReadInfo();
  void ContinueReadData();

  int range_offset_;
  int range_length_;
  int read_position_;
  int reading_metadata_size_;
  base::WeakPtrFactory<AppCacheResponseReader> weak_factory_;
};

// Writes new response data to storage. If the object is deleted
// and there is a write in progress, the implementation will return
// immediately but will take care of any side effect of cancelling the
// operation. In other words, instances are safe to delete at will.
class CONTENT_EXPORT AppCacheResponseWriter
    : public AppCacheResponseIO {
 public:
  ~AppCacheResponseWriter() override;

  // Writes the http info to storage. Always returns the result of the write
  // asynchronously through the 'callback'. Returns the number of bytes written
  // or a net:: error code. The writer acquires a reference to the 'info_buf'
  // until completion at which time the callback is invoked with either a
  // negative error code or the number of bytes written. The 'callback' is a
  // required parameter. The contents of 'info_buf' are not modified.
  // Should only be called where there is no Write operation in progress.
  // (virtual for testing)
  virtual void WriteInfo(HttpResponseInfoIOBuffer* info_buf,
                         const net::CompletionCallback& callback);

  // Writes data to storage. Always returns the result of the write
  // asynchronously through the 'callback'. Returns the number of bytes written
  // or a net:: error code. Guaranteed to not perform partial writes.
  // The writer acquires a reference to the provided 'buf' until completion at
  // which time the callback is invoked with either a negative error code or
  // the number of bytes written. The 'callback' is a required parameter.
  // The contents of 'buf' are not modified.
  // Should only be called where there is no Write operation in progress.
  // (virtual for testing)
  virtual void WriteData(net::IOBuffer* buf,
                         int buf_len,
                         const net::CompletionCallback& callback);

  // Returns true if there is a write pending.
  bool IsWritePending() { return IsIOPending(); }

  // Returns the amount written, info and data.
  int64_t amount_written() { return info_size_ + write_position_; }

 protected:
  // Should only be constructed by the storage class and derivatives.
  AppCacheResponseWriter(
      int64_t response_id,
      int64_t group_id,
      const base::WeakPtr<AppCacheDiskCacheInterface>& disk_cache);

 private:
  friend class AppCacheStorageImpl;
  friend class content::MockAppCacheStorage;

  enum CreationPhase {
    NO_ATTEMPT,
    INITIAL_ATTEMPT,
    DOOM_EXISTING,
    SECOND_ATTEMPT
  };

  void OnIOComplete(int result) override;
  void ContinueWriteInfo();
  void ContinueWriteData();
  void CreateEntryIfNeededAndContinue();
  void OnCreateEntryComplete(AppCacheDiskCacheInterface::Entry** entry, int rv);

  int info_size_;
  int write_position_;
  int write_amount_;
  CreationPhase creation_phase_;
  net::CompletionCallback create_callback_;
  base::WeakPtrFactory<AppCacheResponseWriter> weak_factory_;
};

// Writes metadata of the existing response to storage. If the object is deleted
// and there is a write in progress, the implementation will return
// immediately but will take care of any side effect of cancelling the
// operation. In other words, instances are safe to delete at will.
class CONTENT_EXPORT AppCacheResponseMetadataWriter
    : public AppCacheResponseIO {
 public:
  ~AppCacheResponseMetadataWriter() override;

  // Writes metadata to storage. Always returns the result of the write
  // asynchronously through the 'callback'. Returns the number of bytes written
  // or a net:: error code. Guaranteed to not perform partial writes.
  // The writer acquires a reference to the provided 'buf' until completion at
  // which time the callback is invoked with either a negative error code or
  // the number of bytes written. The 'callback' is a required parameter.
  // The contents of 'buf' are not modified.
  // Should only be called where there is no WriteMetadata operation in
  // progress.
  void WriteMetadata(net::IOBuffer* buf,
                     int buf_len,
                     const net::CompletionCallback& callback);

  // Returns true if there is a write pending.
  bool IsWritePending() { return IsIOPending(); }

 protected:
  friend class AppCacheStorageImpl;
  friend class content::MockAppCacheStorage;

  // Should only be constructed by the storage class and derivatives.
  AppCacheResponseMetadataWriter(
      int64_t response_id,
      int64_t group_id,
      const base::WeakPtr<AppCacheDiskCacheInterface>& disk_cache);

 private:
  void OnIOComplete(int result) override;
  void OnOpenEntryComplete() override;

  int write_amount_;
  base::WeakPtrFactory<AppCacheResponseMetadataWriter> weak_factory_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_APPCACHE_APPCACHE_RESPONSE_H_
