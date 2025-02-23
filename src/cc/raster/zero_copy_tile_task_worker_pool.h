// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_RASTER_ZERO_COPY_TILE_TASK_WORKER_POOL_H_
#define CC_RASTER_ZERO_COPY_TILE_TASK_WORKER_POOL_H_

#include <stdint.h>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "cc/raster/tile_task_runner.h"
#include "cc/raster/tile_task_worker_pool.h"

namespace base {
namespace trace_event {
class ConvertableToTraceFormat;
}
}

namespace cc {
class ResourceProvider;

class CC_EXPORT ZeroCopyTileTaskWorkerPool : public TileTaskWorkerPool,
                                             public TileTaskRunner,
                                             public TileTaskClient {
 public:
  ~ZeroCopyTileTaskWorkerPool() override;

  static std::unique_ptr<TileTaskWorkerPool> Create(
      base::SequencedTaskRunner* task_runner,
      TaskGraphRunner* task_graph_runner,
      ResourceProvider* resource_provider,
      ResourceFormat preferred_tile_format);

  // Overridden from TileTaskWorkerPool:
  TileTaskRunner* AsTileTaskRunner() override;

  // Overridden from TileTaskRunner:
  void Shutdown() override;
  void ScheduleTasks(TaskGraph* graph) override;
  void CheckForCompletedTasks() override;
  ResourceFormat GetResourceFormat(bool must_support_alpha) const override;
  bool GetResourceRequiresSwizzle(bool must_support_alpha) const override;

  // Overridden from TileTaskClient:
  std::unique_ptr<RasterBuffer> AcquireBufferForRaster(
      const Resource* resource,
      uint64_t resource_content_id,
      uint64_t previous_content_id) override;
  void ReleaseBufferForRaster(std::unique_ptr<RasterBuffer> buffer) override;

 protected:
  ZeroCopyTileTaskWorkerPool(base::SequencedTaskRunner* task_runner,
                             TaskGraphRunner* task_graph_runner,
                             ResourceProvider* resource_provider,
                             ResourceFormat preferred_tile_format);

 private:
  std::unique_ptr<base::trace_event::ConvertableToTraceFormat> StateAsValue()
      const;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  TaskGraphRunner* task_graph_runner_;
  const NamespaceToken namespace_token_;
  ResourceProvider* resource_provider_;

  ResourceFormat preferred_tile_format_;

  Task::Vector completed_tasks_;

  DISALLOW_COPY_AND_ASSIGN(ZeroCopyTileTaskWorkerPool);
};

}  // namespace cc

#endif  // CC_RASTER_ZERO_COPY_TILE_TASK_WORKER_POOL_H_
