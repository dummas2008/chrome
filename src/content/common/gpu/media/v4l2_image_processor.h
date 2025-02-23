// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_GPU_MEDIA_V4L2_IMAGE_PROCESSOR_H_
#define CONTENT_COMMON_GPU_MEDIA_V4L2_IMAGE_PROCESSOR_H_

#include <stddef.h>
#include <stdint.h>

#include <queue>
#include <vector>

#include "base/macros.h"
#include "base/memory/linked_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread.h"
#include "content/common/content_export.h"
#include "content/common/gpu/media/v4l2_device.h"
#include "media/base/video_frame.h"

namespace content {

// Handles image processing accelerators that expose a V4L2 memory-to-memory
// interface. The threading model of this class is the same as for other V4L2
// hardware accelerators (see V4L2VideoDecodeAccelerator) for more details.
class CONTENT_EXPORT V4L2ImageProcessor {
 public:
  explicit V4L2ImageProcessor(const scoped_refptr<V4L2Device>& device);
  virtual ~V4L2ImageProcessor();

  // Initializes the processor to convert from |input_format| to |output_format|
  // and/or scale from |input_visible_size| to |output_visible_size|.
  // Request the output buffers to be of at least |output_allocated_size|. The
  // adjusted size will be stored back to |output_allocated_size|. The number of
  // input buffers and output buffers will be |num_buffers|. Provided |error_cb|
  // will be called if an error occurs. Return true if the requested
  // configuration is supported.
  bool Initialize(const base::Closure& error_cb,
                  media::VideoPixelFormat input_format,
                  media::VideoPixelFormat output_format,
                  int num_buffers,
                  gfx::Size input_visible_size,
                  gfx::Size output_visible_size,
                  gfx::Size* output_allocated_size);

  // Return a vector of dmabuf file descriptors, exported for V4L2 output buffer
  // with |index|. The size of vector will be the number of planes of the
  // buffer. Return an empty vector on failure.
  std::vector<base::ScopedFD> GetDmabufsForOutputBuffer(
      int output_buffer_index);

  // Returns allocated size required by the processor to be fed with.
  gfx::Size input_allocated_size() { return input_allocated_size_; }

  // Callback to be used to return the index of a processed image to the
  // client. After the client is done with the frame, call Process with the
  // index to return the output buffer to the image processor.
  typedef base::Callback<void(int output_buffer_index)> FrameReadyCB;

  // Called by client to process |frame|. The resulting processed frame will be
  // stored in |output_buffer_index| output buffer and notified via |cb|. The
  // processor will drop all its references to |frame| after it finishes
  // accessing it.
  void Process(const scoped_refptr<media::VideoFrame>& frame,
               int output_buffer_index,
               const FrameReadyCB& cb);

  // Stop all processing and clean up.
  void Destroy();

 private:
  // Record for input buffers.
  struct InputRecord {
    InputRecord();
    ~InputRecord();
    scoped_refptr<media::VideoFrame> frame;
    bool at_device;
  };

  // Record for output buffers.
  struct OutputRecord {
    OutputRecord();
    ~OutputRecord();
    bool at_device;
  };

  // Job record. Jobs are processed in a FIFO order. This is separate from
  // InputRecord, because an InputRecord may be returned before we dequeue
  // the corresponding output buffer. The processed frame will be stored in
  // |output_buffer_index| output buffer.
  struct JobRecord {
    JobRecord();
    ~JobRecord();
    scoped_refptr<media::VideoFrame> frame;
    int output_buffer_index;
    FrameReadyCB ready_cb;
  };

  void EnqueueInput();
  void EnqueueOutput(int index);
  void Dequeue();
  bool EnqueueInputRecord();
  bool EnqueueOutputRecord(int index);
  bool CreateInputBuffers();
  bool CreateOutputBuffers();
  void DestroyInputBuffers();
  void DestroyOutputBuffers();

  void NotifyError();
  void DestroyTask();

  void ProcessTask(std::unique_ptr<JobRecord> job_record);
  void ServiceDeviceTask();

  // Attempt to start/stop device_poll_thread_.
  bool StartDevicePoll();
  bool StopDevicePoll();

  // Ran on device_poll_thread_ to wait for device events.
  void DevicePollTask(bool poll_device);

  // Size and format-related members remain constant after initialization.
  // The visible/allocated sizes of the input frame.
  gfx::Size input_visible_size_;
  gfx::Size input_allocated_size_;

  // The visible/allocated sizes of the destination frame.
  gfx::Size output_visible_size_;
  gfx::Size output_allocated_size_;

  media::VideoPixelFormat input_format_;
  media::VideoPixelFormat output_format_;
  uint32_t input_format_fourcc_;
  uint32_t output_format_fourcc_;

  size_t input_planes_count_;
  size_t output_planes_count_;

  // Our original calling task runner for the child thread.
  const scoped_refptr<base::SingleThreadTaskRunner> child_task_runner_;

  // V4L2 device in use.
  scoped_refptr<V4L2Device> device_;

  // Thread to communicate with the device on.
  base::Thread device_thread_;
  // Thread used to poll the V4L2 for events only.
  base::Thread device_poll_thread_;

  // All the below members are to be accessed from device_thread_ only
  // (if it's running).
  std::queue<linked_ptr<JobRecord> > input_queue_;
  std::queue<linked_ptr<JobRecord> > running_jobs_;

  // Input queue state.
  bool input_streamon_;
  // Number of input buffers enqueued to the device.
  int input_buffer_queued_count_;
  // Input buffers ready to use; LIFO since we don't care about ordering.
  std::vector<int> free_input_buffers_;
  // Mapping of int index to an input buffer record.
  std::vector<InputRecord> input_buffer_map_;

  // Output queue state.
  bool output_streamon_;
  // Number of output buffers enqueued to the device.
  int output_buffer_queued_count_;
  // Mapping of int index to an output buffer record.
  std::vector<OutputRecord> output_buffer_map_;
  // The number of input or output buffers.
  int num_buffers_;

  // Error callback to the client.
  base::Closure error_cb_;

  // Weak factory for producing weak pointers on the device_thread_
  base::WeakPtrFactory<V4L2ImageProcessor> device_weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(V4L2ImageProcessor);
};

}  // namespace content

#endif  // CONTENT_COMMON_GPU_MEDIA_V4L2_IMAGE_PROCESSOR_H_
