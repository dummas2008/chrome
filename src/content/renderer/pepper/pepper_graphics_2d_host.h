// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_PEPPER_GRAPHICS_2D_HOST_H_
#define CONTENT_RENDERER_PEPPER_PEPPER_GRAPHICS_2D_HOST_H_

#include <stdint.h>

#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "content/common/content_export.h"
#include "ppapi/c/ppb_graphics_2d.h"
#include "ppapi/host/host_message_context.h"
#include "ppapi/host/resource_host.h"
#include "third_party/WebKit/public/platform/WebCanvas.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"

namespace cc {
class SharedBitmap;
class SingleReleaseCallback;
class TextureMailbox;
}

namespace gfx {
class Rect;
}

namespace gpu {
struct SyncToken;
}

namespace ppapi {
struct ViewData;
}

namespace content {

class PepperPluginInstanceImpl;
class PPB_ImageData_Impl;
class RendererPpapiHost;

class CONTENT_EXPORT PepperGraphics2DHost
    : public ppapi::host::ResourceHost,
      public base::SupportsWeakPtr<PepperGraphics2DHost> {
 public:
  static PepperGraphics2DHost* Create(
      RendererPpapiHost* host,
      PP_Instance instance,
      PP_Resource resource,
      const PP_Size& size,
      PP_Bool is_always_opaque,
      scoped_refptr<PPB_ImageData_Impl> backing_store);

  ~PepperGraphics2DHost() override;

  // ppapi::host::ResourceHost override.
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;
  bool IsGraphics2DHost() override;

  bool ReadImageData(PP_Resource image, const PP_Point* top_left);
  // Assciates this device with the given plugin instance. You can pass NULL
  // to clear the existing device. Returns true on success. In this case, a
  // repaint of the page will also be scheduled. Failure means that the device
  // is already bound to a different instance, and nothing will happen.
  bool BindToInstance(PepperPluginInstanceImpl* new_instance);
  // Paints the current backing store to the web page.
  void Paint(blink::WebCanvas* canvas,
             const gfx::Rect& plugin_rect,
             const gfx::Rect& paint_rect);

  bool PrepareTextureMailbox(
      cc::TextureMailbox* mailbox,
      std::unique_ptr<cc::SingleReleaseCallback>* release_callback);
  void AttachedToNewLayer();

  // Notifications about the view's progress painting.  See PluginInstance.
  // These messages are used to send Flush callbacks to the plugin.
  void ViewInitiatedPaint();

  void SetScale(float scale);
  float GetScale() const;
  bool IsAlwaysOpaque() const;
  PPB_ImageData_Impl* ImageData();
  gfx::Size Size() const;

  void ClearCache();

 private:
  PepperGraphics2DHost(RendererPpapiHost* host,
                       PP_Instance instance,
                       PP_Resource resource);

  bool Init(int width,
            int height,
            bool is_always_opaque,
            scoped_refptr<PPB_ImageData_Impl> backing_store);

  int32_t OnHostMsgPaintImageData(ppapi::host::HostMessageContext* context,
                                  const ppapi::HostResource& image_data,
                                  const PP_Point& top_left,
                                  bool src_rect_specified,
                                  const PP_Rect& src_rect);
  int32_t OnHostMsgScroll(ppapi::host::HostMessageContext* context,
                          bool clip_specified,
                          const PP_Rect& clip,
                          const PP_Point& amount);
  int32_t OnHostMsgReplaceContents(ppapi::host::HostMessageContext* context,
                                   const ppapi::HostResource& image_data);
  int32_t OnHostMsgFlush(ppapi::host::HostMessageContext* context);
  int32_t OnHostMsgSetScale(ppapi::host::HostMessageContext* context,
                            float scale);
  int32_t OnHostMsgReadImageData(ppapi::host::HostMessageContext* context,
                                 PP_Resource image,
                                 const PP_Point& top_left);

  // If |old_image_data| is not NULL, a previous used ImageData object will be
  // reused.  This is used by ReplaceContents.
  int32_t Flush(PP_Resource* old_image_data);

  // Called internally to execute the different queued commands. The
  // parameters to these functions will have already been validated. The last
  // rect argument will be filled by each function with the area affected by
  // the update that requires invalidation. If there were no pixels changed,
  // this rect can be untouched.
  void ExecutePaintImageData(PPB_ImageData_Impl* image,
                             int x,
                             int y,
                             const gfx::Rect& src_rect,
                             gfx::Rect* invalidated_rect);
  void ExecuteScroll(const gfx::Rect& clip,
                     int dx,
                     int dy,
                     gfx::Rect* invalidated_rect);
  void ExecuteReplaceContents(PPB_ImageData_Impl* image,
                              gfx::Rect* invalidated_rect,
                              PP_Resource* old_image_data);

  void SendFlushAck();

  // Function scheduled to execute by ScheduleOffscreenFlushAck that actually
  // issues the offscreen callbacks.
  void SendOffscreenFlushAck();

  // Schedules the offscreen flush ACK at a future time.
  void ScheduleOffscreenFlushAck();

  // Returns true if there is any type of flush callback pending.
  bool HasPendingFlush() const;

  // Scale |op_rect| to logical pixels, taking care to include partially-
  // covered logical pixels (aka DIPs). Also scale optional |delta| to logical
  // pixels as well for scrolling cases. Returns false for scrolling cases where
  // scaling either |op_rect| or |delta| would require scrolling to fall back to
  // invalidation due to rounding errors, true otherwise.
  static bool ConvertToLogicalPixels(float scale,
                                     gfx::Rect* op_rect,
                                     gfx::Point* delta);

  void ReleaseCallback(std::unique_ptr<cc::SharedBitmap> bitmap,
                       const gfx::Size& bitmap_size,
                       const gpu::SyncToken& sync_token,
                       bool lost_resource);

  RendererPpapiHost* renderer_ppapi_host_;

  scoped_refptr<PPB_ImageData_Impl> image_data_;

  // Non-owning pointer to the plugin instance this context is currently bound
  // to, if any. If the context is currently unbound, this will be NULL.
  PepperPluginInstanceImpl* bound_instance_;

  // Keeps track of all drawing commands queued before a Flush call.
  struct QueuedOperation;
  typedef std::vector<QueuedOperation> OperationQueue;
  OperationQueue queued_operations_;

  // True if we need to send an ACK to plugin.
  bool need_flush_ack_;

  // When doing offscreen flushes, we issue a task that issues the callback
  // later. This is set when one of those tasks is pending so that we can
  // enforce the "only one pending flush at a time" constraint in the API.
  bool offscreen_flush_pending_;

  // Set to true if the plugin declares that this device will always be opaque.
  // This allows us to do more optimized painting in some cases.
  bool is_always_opaque_;

  // Set to the scale between what the plugin considers to be one pixel and one
  // DIP
  float scale_;

  ppapi::host::ReplyMessageContext flush_reply_context_;

  bool is_running_in_process_;

  bool texture_mailbox_modified_;
  bool is_using_texture_layer_;

  // This is a bitmap that was recently released by the compositor and may be
  // used to transfer bytes to the compositor again.
  std::unique_ptr<cc::SharedBitmap> cached_bitmap_;
  gfx::Size cached_bitmap_size_;

  friend class PepperGraphics2DHostTest;
  DISALLOW_COPY_AND_ASSIGN(PepperGraphics2DHost);
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_PEPPER_GRAPHICS_2D_HOST_H_
