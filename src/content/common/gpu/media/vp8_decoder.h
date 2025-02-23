// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_MEDIA_VP8_DECODER_H_
#define CONTENT_COMMON_GPU_MEDIA_VP8_DECODER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "content/common/gpu/media/accelerated_video_decoder.h"
#include "content/common/gpu/media/vp8_picture.h"
#include "media/filters/vp8_parser.h"

namespace content {

// Clients of this class are expected to pass raw VP8 stream and are expected
// to provide an implementation of VP8Accelerator for offloading final steps
// of the decoding process.
//
// This class must be created, called and destroyed on a single thread, and
// does nothing internally on any other thread.
class CONTENT_EXPORT VP8Decoder : public AcceleratedVideoDecoder {
 public:
  class CONTENT_EXPORT VP8Accelerator {
   public:
    VP8Accelerator();
    virtual ~VP8Accelerator();

    // Create a new VP8Picture that the decoder client can use for decoding
    // and pass back to this accelerator for decoding or reference.
    // When the picture is no longer needed by decoder, it will just drop
    // its reference to it, and it may do so at any time.
    // Note that this may return nullptr if accelerator is not able to provide
    // any new pictures at given time. The decoder is expected to handle
    // this situation as normal and return from Decode() with kRanOutOfSurfaces.
    virtual scoped_refptr<VP8Picture> CreateVP8Picture() = 0;

    // Submit decode for |pic|, taking as arguments |frame_hdr| with parsed
    // VP8 frame header information for current frame, and using |last_frame|,
    // |golden_frame| and |alt_frame| as references, as per VP8 specification.
    // Note that this runs the decode in hardware.
    // Return true if successful.
    virtual bool SubmitDecode(const scoped_refptr<VP8Picture>& pic,
                              const media::Vp8FrameHeader* frame_hdr,
                              const scoped_refptr<VP8Picture>& last_frame,
                              const scoped_refptr<VP8Picture>& golden_frame,
                              const scoped_refptr<VP8Picture>& alt_frame) = 0;

    // Schedule output (display) of |pic|. Note that returning from this
    // method does not mean that |pic| has already been outputted (displayed),
    // but guarantees that all pictures will be outputted in the same order
    // as this method was called for them. Decoder may drop its reference
    // to |pic| after calling this method.
    // Return true if successful.
    virtual bool OutputPicture(const scoped_refptr<VP8Picture>& pic) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(VP8Accelerator);
  };

  VP8Decoder(VP8Accelerator* accelerator);
  ~VP8Decoder() override;

  // content::AcceleratedVideoDecoder implementation.
  bool Flush() override WARN_UNUSED_RESULT;
  void Reset() override;
  void SetStream(const uint8_t* ptr, size_t size) override;
  DecodeResult Decode() override WARN_UNUSED_RESULT;
  gfx::Size GetPicSize() const override;
  size_t GetRequiredNumOfPictures() const override;

 private:
  bool DecodeAndOutputCurrentFrame();
  void RefreshReferenceFrames();

  enum State {
    kNeedStreamMetadata,  // After initialization, need a keyframe.
    kDecoding,            // Ready to decode from any point.
    kAfterReset,          // After Reset(), need a resume point.
    kError,               // Error in decode, can't continue.
  };

  State state_;

  media::Vp8Parser parser_;

  std::unique_ptr<media::Vp8FrameHeader> curr_frame_hdr_;
  scoped_refptr<VP8Picture> curr_pic_;
  scoped_refptr<VP8Picture> last_frame_;
  scoped_refptr<VP8Picture> golden_frame_;
  scoped_refptr<VP8Picture> alt_frame_;

  const uint8_t* curr_frame_start_;
  size_t frame_size_;

  gfx::Size pic_size_;
  int horizontal_scale_;
  int vertical_scale_;

  VP8Accelerator* accelerator_;

  DISALLOW_COPY_AND_ASSIGN(VP8Decoder);
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_MEDIA_VP8_DECODER_H_
