// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper/pepper_video_decoder_host.h"

#include <stddef.h>

#include "base/bind.h"
#include "base/memory/shared_memory.h"
#include "build/build_config.h"
#include "content/common/pepper_file_util.h"
#include "content/public/common/content_client.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/renderer_ppapi_host.h"
#include "content/renderer/pepper/gfx_conversion.h"
#include "content/renderer/pepper/ppb_graphics_3d_impl.h"
#include "content/renderer/pepper/video_decoder_shim.h"
#include "gpu/ipc/client/command_buffer_proxy_impl.h"
#include "media/base/limits.h"
#include "media/gpu/ipc/client/gpu_video_decode_accelerator_host.h"
#include "media/video/video_decode_accelerator.h"
#include "ppapi/c/pp_completion_callback.h"
#include "ppapi/c/pp_errors.h"
#include "ppapi/host/dispatch_host_message.h"
#include "ppapi/host/ppapi_host.h"
#include "ppapi/proxy/ppapi_messages.h"
#include "ppapi/proxy/video_decoder_constants.h"
#include "ppapi/thunk/enter.h"
#include "ppapi/thunk/ppb_graphics_3d_api.h"

using ppapi::proxy::SerializedHandle;
using ppapi::thunk::EnterResourceNoLock;
using ppapi::thunk::PPB_Graphics3D_API;

namespace content {

namespace {

media::VideoCodecProfile PepperToMediaVideoProfile(PP_VideoProfile profile) {
  switch (profile) {
    case PP_VIDEOPROFILE_H264BASELINE:
      return media::H264PROFILE_BASELINE;
    case PP_VIDEOPROFILE_H264MAIN:
      return media::H264PROFILE_MAIN;
    case PP_VIDEOPROFILE_H264EXTENDED:
      return media::H264PROFILE_EXTENDED;
    case PP_VIDEOPROFILE_H264HIGH:
      return media::H264PROFILE_HIGH;
    case PP_VIDEOPROFILE_H264HIGH10PROFILE:
      return media::H264PROFILE_HIGH10PROFILE;
    case PP_VIDEOPROFILE_H264HIGH422PROFILE:
      return media::H264PROFILE_HIGH422PROFILE;
    case PP_VIDEOPROFILE_H264HIGH444PREDICTIVEPROFILE:
      return media::H264PROFILE_HIGH444PREDICTIVEPROFILE;
    case PP_VIDEOPROFILE_H264SCALABLEBASELINE:
      return media::H264PROFILE_SCALABLEBASELINE;
    case PP_VIDEOPROFILE_H264SCALABLEHIGH:
      return media::H264PROFILE_SCALABLEHIGH;
    case PP_VIDEOPROFILE_H264STEREOHIGH:
      return media::H264PROFILE_STEREOHIGH;
    case PP_VIDEOPROFILE_H264MULTIVIEWHIGH:
      return media::H264PROFILE_MULTIVIEWHIGH;
    case PP_VIDEOPROFILE_VP8_ANY:
      return media::VP8PROFILE_ANY;
    case PP_VIDEOPROFILE_VP9_ANY:
      return media::VP9PROFILE_PROFILE0;
    // No default case, to catch unhandled PP_VideoProfile values.
  }

  return media::VIDEO_CODEC_PROFILE_UNKNOWN;
}

}  // namespace

PepperVideoDecoderHost::PendingDecode::PendingDecode(
    int32_t decode_id,
    uint32_t shm_id,
    uint32_t size,
    const ppapi::host::ReplyMessageContext& reply_context)
    : decode_id(decode_id),
      shm_id(shm_id),
      size(size),
      reply_context(reply_context) {}

PepperVideoDecoderHost::PendingDecode::~PendingDecode() {}

PepperVideoDecoderHost::PepperVideoDecoderHost(RendererPpapiHost* host,
                                               PP_Instance instance,
                                               PP_Resource resource)
    : ResourceHost(host->GetPpapiHost(), instance, resource),
      renderer_ppapi_host_(host) {}

PepperVideoDecoderHost::~PepperVideoDecoderHost() {}

int32_t PepperVideoDecoderHost::OnResourceMessageReceived(
    const IPC::Message& msg,
    ppapi::host::HostMessageContext* context) {
  PPAPI_BEGIN_MESSAGE_MAP(PepperVideoDecoderHost, msg)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_VideoDecoder_Initialize,
                                      OnHostMsgInitialize)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_VideoDecoder_GetShm,
                                      OnHostMsgGetShm)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_VideoDecoder_Decode,
                                      OnHostMsgDecode)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_VideoDecoder_AssignTextures,
                                      OnHostMsgAssignTextures)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL(PpapiHostMsg_VideoDecoder_RecyclePicture,
                                      OnHostMsgRecyclePicture)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(PpapiHostMsg_VideoDecoder_Flush,
                                        OnHostMsgFlush)
    PPAPI_DISPATCH_HOST_RESOURCE_CALL_0(PpapiHostMsg_VideoDecoder_Reset,
                                        OnHostMsgReset)
  PPAPI_END_MESSAGE_MAP()
  return PP_ERROR_FAILED;
}

int32_t PepperVideoDecoderHost::OnHostMsgInitialize(
    ppapi::host::HostMessageContext* context,
    const ppapi::HostResource& graphics_context,
    PP_VideoProfile profile,
    PP_HardwareAcceleration acceleration,
    uint32_t min_picture_count) {
  if (initialized_)
    return PP_ERROR_FAILED;
  if (min_picture_count > ppapi::proxy::kMaximumPictureCount)
    return PP_ERROR_BADARGUMENT;

  EnterResourceNoLock<PPB_Graphics3D_API> enter_graphics(
      graphics_context.host_resource(), true);
  if (enter_graphics.failed())
    return PP_ERROR_FAILED;
  PPB_Graphics3D_Impl* graphics3d =
      static_cast<PPB_Graphics3D_Impl*>(enter_graphics.object());

  gpu::CommandBufferProxyImpl* command_buffer =
      graphics3d->GetCommandBufferProxy();
  if (!command_buffer)
    return PP_ERROR_FAILED;

  profile_ = PepperToMediaVideoProfile(profile);
  software_fallback_allowed_ = (acceleration != PP_HARDWAREACCELERATION_ONLY);

  min_picture_count_ = min_picture_count;

  if (acceleration != PP_HARDWAREACCELERATION_NONE) {
    // This is not synchronous, but subsequent IPC messages will be buffered, so
    // it is okay to immediately send IPC messages.
    gpu::GpuChannelHost* channel = command_buffer->channel();
    if (channel) {
      decoder_.reset(
          new media::GpuVideoDecodeAcceleratorHost(channel, command_buffer));
      if (decoder_->Initialize(profile_, this)) {
        initialized_ = true;
        return PP_OK;
      }
    }
    decoder_.reset();
    if (acceleration == PP_HARDWAREACCELERATION_ONLY)
      return PP_ERROR_NOTSUPPORTED;
  }

#if defined(OS_ANDROID)
  return PP_ERROR_NOTSUPPORTED;
#else
  if (!TryFallbackToSoftwareDecoder())
    return PP_ERROR_FAILED;

  initialized_ = true;
  return PP_OK;
#endif
}

int32_t PepperVideoDecoderHost::OnHostMsgGetShm(
    ppapi::host::HostMessageContext* context,
    uint32_t shm_id,
    uint32_t shm_size) {
  if (!initialized_)
    return PP_ERROR_FAILED;

  // Make the buffers larger since we hope to reuse them.
  shm_size = std::max(
      shm_size,
      static_cast<uint32_t>(ppapi::proxy::kMinimumBitstreamBufferSize));
  if (shm_size > ppapi::proxy::kMaximumBitstreamBufferSize)
    return PP_ERROR_FAILED;

  if (shm_id >= ppapi::proxy::kMaximumPendingDecodes)
    return PP_ERROR_FAILED;
  // The shm_id must be inside or at the end of shm_buffers_.
  if (shm_id > shm_buffers_.size())
    return PP_ERROR_FAILED;
  // Reject an attempt to reallocate a busy shm buffer.
  if (shm_id < shm_buffers_.size() && shm_buffer_busy_[shm_id])
    return PP_ERROR_FAILED;

  content::RenderThread* render_thread = content::RenderThread::Get();
  std::unique_ptr<base::SharedMemory> shm(
      render_thread->HostAllocateSharedMemoryBuffer(shm_size));
  if (!shm || !shm->Map(shm_size))
    return PP_ERROR_FAILED;

  base::SharedMemoryHandle shm_handle = shm->handle();
  if (shm_id == shm_buffers_.size()) {
    shm_buffers_.push_back(shm.release());
    shm_buffer_busy_.push_back(false);
  } else {
    // Remove the old buffer. Delete manually since ScopedVector won't delete
    // the existing element if we just assign over it.
    delete shm_buffers_[shm_id];
    shm_buffers_[shm_id] = shm.release();
  }

  SerializedHandle handle(
      renderer_ppapi_host_->ShareSharedMemoryHandleWithRemote(shm_handle),
      shm_size);
  ppapi::host::ReplyMessageContext reply_context =
      context->MakeReplyMessageContext();
  reply_context.params.AppendHandle(handle);
  host()->SendReply(reply_context,
                    PpapiPluginMsg_VideoDecoder_GetShmReply(shm_size));

  return PP_OK_COMPLETIONPENDING;
}

int32_t PepperVideoDecoderHost::OnHostMsgDecode(
    ppapi::host::HostMessageContext* context,
    uint32_t shm_id,
    uint32_t size,
    int32_t decode_id) {
  if (!initialized_)
    return PP_ERROR_FAILED;
  DCHECK(decoder_);
  // |shm_id| is just an index into shm_buffers_. Make sure it's in range.
  if (static_cast<size_t>(shm_id) >= shm_buffers_.size())
    return PP_ERROR_FAILED;
  // Reject an attempt to pass a busy buffer to the decoder again.
  if (shm_buffer_busy_[shm_id])
    return PP_ERROR_FAILED;
  // Reject non-unique decode_id values.
  if (GetPendingDecodeById(decode_id) != pending_decodes_.end())
    return PP_ERROR_FAILED;

  if (flush_reply_context_.is_valid() || reset_reply_context_.is_valid())
    return PP_ERROR_FAILED;

  pending_decodes_.push_back(PendingDecode(decode_id, shm_id, size,
                                           context->MakeReplyMessageContext()));

  shm_buffer_busy_[shm_id] = true;
  decoder_->Decode(
      media::BitstreamBuffer(decode_id, shm_buffers_[shm_id]->handle(), size));

  return PP_OK_COMPLETIONPENDING;
}

int32_t PepperVideoDecoderHost::OnHostMsgAssignTextures(
    ppapi::host::HostMessageContext* context,
    const PP_Size& size,
    const std::vector<uint32_t>& texture_ids) {
  if (!initialized_)
    return PP_ERROR_FAILED;
  DCHECK(decoder_);

  // Verify that the new texture IDs are unique and store them in
  // |new_textures|.
  PictureBufferMap new_textures;
  for (uint32_t i = 0; i < texture_ids.size(); i++) {
    if (picture_buffer_map_.find(texture_ids[i]) != picture_buffer_map_.end() ||
        new_textures.find(texture_ids[i]) != new_textures.end()) {
      // Can't assign the same texture more than once.
      return PP_ERROR_BADARGUMENT;
    }
    new_textures.insert(
        std::make_pair(texture_ids[i], PictureBufferState::ASSIGNED));
  }

  picture_buffer_map_.insert(new_textures.begin(), new_textures.end());

  std::vector<media::PictureBuffer> picture_buffers;
  for (uint32_t i = 0; i < texture_ids.size(); i++) {
    media::PictureBuffer::TextureIds ids;
    ids.push_back(texture_ids[i]);
    media::PictureBuffer buffer(
        texture_ids[i],  // Use the texture_id to identify the buffer.
        gfx::Size(size.width, size.height), ids);
    picture_buffers.push_back(buffer);
  }
  decoder_->AssignPictureBuffers(picture_buffers);
  return PP_OK;
}

int32_t PepperVideoDecoderHost::OnHostMsgRecyclePicture(
    ppapi::host::HostMessageContext* context,
    uint32_t texture_id) {
  if (!initialized_)
    return PP_ERROR_FAILED;
  DCHECK(decoder_);

  PictureBufferMap::iterator it = picture_buffer_map_.find(texture_id);
  if (it == picture_buffer_map_.end())
    return PP_ERROR_BADARGUMENT;

  switch (it->second) {
    case PictureBufferState::ASSIGNED:
      return PP_ERROR_BADARGUMENT;

    case PictureBufferState::IN_USE:
      it->second = PictureBufferState::ASSIGNED;
      decoder_->ReusePictureBuffer(texture_id);
      break;

    case PictureBufferState::DISMISSED:
      picture_buffer_map_.erase(it);
      // The texture was already dismissed by the decoder. Notify the plugin.
      host()->SendUnsolicitedReply(
          pp_resource(),
          PpapiPluginMsg_VideoDecoder_DismissPicture(texture_id));
      break;
  }

  return PP_OK;
}

int32_t PepperVideoDecoderHost::OnHostMsgFlush(
    ppapi::host::HostMessageContext* context) {
  if (!initialized_)
    return PP_ERROR_FAILED;
  DCHECK(decoder_);
  if (flush_reply_context_.is_valid() || reset_reply_context_.is_valid())
    return PP_ERROR_FAILED;

  flush_reply_context_ = context->MakeReplyMessageContext();
  decoder_->Flush();

  return PP_OK_COMPLETIONPENDING;
}

int32_t PepperVideoDecoderHost::OnHostMsgReset(
    ppapi::host::HostMessageContext* context) {
  if (!initialized_)
    return PP_ERROR_FAILED;
  DCHECK(decoder_);
  if (flush_reply_context_.is_valid() || reset_reply_context_.is_valid())
    return PP_ERROR_FAILED;

  reset_reply_context_ = context->MakeReplyMessageContext();
  decoder_->Reset();

  return PP_OK_COMPLETIONPENDING;
}

void PepperVideoDecoderHost::ProvidePictureBuffers(
    uint32_t requested_num_of_buffers,
    uint32_t textures_per_buffer,
    const gfx::Size& dimensions,
    uint32_t texture_target) {
  DCHECK_EQ(1u, textures_per_buffer);
  RequestTextures(std::max(min_picture_count_, requested_num_of_buffers),
                  dimensions,
                  texture_target,
                  std::vector<gpu::Mailbox>());
}

void PepperVideoDecoderHost::PictureReady(const media::Picture& picture) {
  PictureBufferMap::iterator it =
      picture_buffer_map_.find(picture.picture_buffer_id());
  DCHECK(it != picture_buffer_map_.end());
  DCHECK(it->second == PictureBufferState::ASSIGNED);
  it->second = PictureBufferState::IN_USE;

  // Don't bother validating the visible rect, since the plugin process is less
  // trusted than the gpu process.
  PP_Rect visible_rect = PP_FromGfxRect(picture.visible_rect());
  host()->SendUnsolicitedReply(pp_resource(),
                               PpapiPluginMsg_VideoDecoder_PictureReady(
                                   picture.bitstream_buffer_id(),
                                   picture.picture_buffer_id(), visible_rect));
}

void PepperVideoDecoderHost::DismissPictureBuffer(int32_t picture_buffer_id) {
  PictureBufferMap::iterator it = picture_buffer_map_.find(picture_buffer_id);
  DCHECK(it != picture_buffer_map_.end());

  // If the texture is still used by the plugin keep it until the plugin
  // recycles it.
  if (it->second == PictureBufferState::IN_USE) {
    it->second = PictureBufferState::DISMISSED;
    return;
  }

  DCHECK(it->second == PictureBufferState::ASSIGNED);
  picture_buffer_map_.erase(it);
  host()->SendUnsolicitedReply(
      pp_resource(),
      PpapiPluginMsg_VideoDecoder_DismissPicture(picture_buffer_id));
}

void PepperVideoDecoderHost::NotifyEndOfBitstreamBuffer(
    int32_t bitstream_buffer_id) {
  PendingDecodeList::iterator it = GetPendingDecodeById(bitstream_buffer_id);
  if (it == pending_decodes_.end()) {
    NOTREACHED();
    return;
  }
  host()->SendReply(it->reply_context,
                    PpapiPluginMsg_VideoDecoder_DecodeReply(it->shm_id));
  shm_buffer_busy_[it->shm_id] = false;
  pending_decodes_.erase(it);
}

void PepperVideoDecoderHost::NotifyFlushDone() {
  DCHECK(pending_decodes_.empty());
  host()->SendReply(flush_reply_context_,
                    PpapiPluginMsg_VideoDecoder_FlushReply());
  flush_reply_context_ = ppapi::host::ReplyMessageContext();
}

void PepperVideoDecoderHost::NotifyResetDone() {
  DCHECK(pending_decodes_.empty());
  host()->SendReply(reset_reply_context_,
                    PpapiPluginMsg_VideoDecoder_ResetReply());
  reset_reply_context_ = ppapi::host::ReplyMessageContext();
}

void PepperVideoDecoderHost::NotifyError(
    media::VideoDecodeAccelerator::Error error) {
  int32_t pp_error = PP_ERROR_FAILED;
  switch (error) {
    case media::VideoDecodeAccelerator::UNREADABLE_INPUT:
      pp_error = PP_ERROR_MALFORMED_INPUT;
      break;
    case media::VideoDecodeAccelerator::ILLEGAL_STATE:
    case media::VideoDecodeAccelerator::INVALID_ARGUMENT:
    case media::VideoDecodeAccelerator::PLATFORM_FAILURE:
      pp_error = PP_ERROR_RESOURCE_FAILED;
      break;
    // No default case, to catch unhandled enum values.
  }

  // Try to initialize software decoder and use it instead.
  if (!software_fallback_used_ && software_fallback_allowed_) {
    VLOG(0)
        << "Hardware decoder has returned an error. Trying Software decoder.";
    if (TryFallbackToSoftwareDecoder())
      return;
  }

  host()->SendUnsolicitedReply(
      pp_resource(), PpapiPluginMsg_VideoDecoder_NotifyError(pp_error));
}

const uint8_t* PepperVideoDecoderHost::DecodeIdToAddress(uint32_t decode_id) {
  PendingDecodeList::const_iterator it = GetPendingDecodeById(decode_id);
  DCHECK(it != pending_decodes_.end());
  uint32_t shm_id = it->shm_id;
  return static_cast<uint8_t*>(shm_buffers_[shm_id]->memory());
}

void PepperVideoDecoderHost::RequestTextures(
    uint32_t requested_num_of_buffers,
    const gfx::Size& dimensions,
    uint32_t texture_target,
    const std::vector<gpu::Mailbox>& mailboxes) {
  host()->SendUnsolicitedReply(
      pp_resource(),
      PpapiPluginMsg_VideoDecoder_RequestTextures(
          requested_num_of_buffers,
          PP_MakeSize(dimensions.width(), dimensions.height()),
          texture_target,
          mailboxes));
}

bool PepperVideoDecoderHost::TryFallbackToSoftwareDecoder() {
#if defined(OS_ANDROID)
  return false;
#else
  DCHECK(!software_fallback_used_ && software_fallback_allowed_);

  uint32_t shim_texture_pool_size = media::limits::kMaxVideoFrames + 1;
  shim_texture_pool_size = std::max(shim_texture_pool_size,
                                    min_picture_count_);
  std::unique_ptr<VideoDecoderShim> new_decoder(
      new VideoDecoderShim(this, shim_texture_pool_size));
  if (!new_decoder->Initialize(profile_, this))
    return false;

  software_fallback_used_ = true;
  decoder_.reset(new_decoder.release());

  // Dismiss all assigned pictures and mark all pictures in use as DISMISSED.
  PictureBufferMap pictures_pending_dismission;
  for (auto& picture : picture_buffer_map_) {
    if (picture.second == PictureBufferState::ASSIGNED) {
      host()->SendUnsolicitedReply(
          pp_resource(),
          PpapiPluginMsg_VideoDecoder_DismissPicture(picture.first));
    } else {
      pictures_pending_dismission.insert(
          std::make_pair(picture.first, PictureBufferState::DISMISSED));
    }
  }
  picture_buffer_map_.swap(pictures_pending_dismission);

  // If there was a pending Reset() it can be finished now.
  if (reset_reply_context_.is_valid()) {
    while (!pending_decodes_.empty()) {
      const PendingDecode& decode = pending_decodes_.front();
      host()->SendReply(decode.reply_context,
                        PpapiPluginMsg_VideoDecoder_DecodeReply(decode.shm_id));
      DCHECK(shm_buffer_busy_[decode.shm_id]);
      shm_buffer_busy_[decode.shm_id] = false;
      pending_decodes_.pop_front();
    }
    NotifyResetDone();
  }

  // Resubmit all pending decodes.
  for (const PendingDecode& decode : pending_decodes_) {
    DCHECK(shm_buffer_busy_[decode.shm_id]);
    decoder_->Decode(media::BitstreamBuffer(
        decode.decode_id, shm_buffers_[decode.shm_id]->handle(), decode.size));
  }

  // Flush the new decoder if Flush() was pending.
  if (flush_reply_context_.is_valid())
    decoder_->Flush();

  return true;
#endif
}

PepperVideoDecoderHost::PendingDecodeList::iterator
PepperVideoDecoderHost::GetPendingDecodeById(int32_t decode_id) {
  return std::find_if(pending_decodes_.begin(), pending_decodes_.end(),
                      [decode_id](const PendingDecode& item) {
                        return item.decode_id == decode_id;
                      });
}

}  // namespace content
