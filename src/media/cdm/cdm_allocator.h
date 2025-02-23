// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CDM_CDM_ALLOCATOR_H_
#define MEDIA_CDM_CDM_ALLOCATOR_H_

#include <stddef.h>
#include <stdint.h>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "media/base/media_export.h"

namespace cdm {
class Buffer;
}

namespace media {

class VideoFrameImpl;

class MEDIA_EXPORT CdmAllocator {
 public:
  virtual ~CdmAllocator();

  // Creates a buffer with at least |capacity| bytes. Caller is required to
  // call Destroy() on the returned buffer when it is done with it.
  virtual cdm::Buffer* CreateCdmBuffer(size_t capacity) = 0;

  // Returns a new VideoFrameImpl.
  virtual scoped_ptr<VideoFrameImpl> CreateCdmVideoFrame() = 0;

 protected:
  CdmAllocator();

 private:
  DISALLOW_COPY_AND_ASSIGN(CdmAllocator);
};

}  // namespace media

#endif  // MEDIA_CDM_CDM_ALLOCATOR_H_
