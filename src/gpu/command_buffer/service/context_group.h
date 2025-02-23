// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_CONTEXT_GROUP_H_
#define GPU_COMMAND_BUFFER_SERVICE_CONTEXT_GROUP_H_

#include <stdint.h>

#include <vector>
#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/common/gles2_cmd_format.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "gpu/command_buffer/service/framebuffer_completeness_cache.h"
#include "gpu/command_buffer/service/gpu_preferences.h"
#include "gpu/command_buffer/service/shader_translator_cache.h"
#include "gpu/gpu_export.h"

namespace gpu {

struct GpuPreferences;
class TransferBufferManager;
class ValueStateMap;

namespace gles2 {

class ProgramCache;
class BufferManager;
class GLES2Decoder;
class FramebufferManager;
class MailboxManager;
class RenderbufferManager;
class PathManager;
class ProgramManager;
class SamplerManager;
class ShaderManager;
class TextureManager;
class SubscriptionRefSet;
class ValuebufferManager;
class MemoryTracker;
struct DisallowedFeatures;

// A Context Group helps manage multiple GLES2Decoders that share
// resources.
class GPU_EXPORT ContextGroup : public base::RefCounted<ContextGroup> {
 public:
  ContextGroup(
      const GpuPreferences& gpu_preferences,
      const scoped_refptr<MailboxManager>& mailbox_manager,
      const scoped_refptr<MemoryTracker>& memory_tracker,
      const scoped_refptr<ShaderTranslatorCache>& shader_translator_cache,
      const scoped_refptr<FramebufferCompletenessCache>&
          framebuffer_completeness_cache,
      const scoped_refptr<FeatureInfo>& feature_info,
      const scoped_refptr<SubscriptionRefSet>& subscription_ref_set,
      const scoped_refptr<ValueStateMap>& pending_valuebuffer_state,
      bool bind_generates_resource);

  // This should only be called by GLES2Decoder. This must be paired with a
  // call to destroy if it succeeds.
  bool Initialize(
      GLES2Decoder* decoder,
      ContextType context_type,
      const DisallowedFeatures& disallowed_features);

  // Destroys all the resources when called for the last context in the group.
  // It should only be called by GLES2Decoder.
  void Destroy(GLES2Decoder* decoder, bool have_context);

  MailboxManager* mailbox_manager() const {
    return mailbox_manager_.get();
  }

  MemoryTracker* memory_tracker() const {
    return memory_tracker_.get();
  }

  ShaderTranslatorCache* shader_translator_cache() const {
    return shader_translator_cache_.get();
  }

  FramebufferCompletenessCache* framebuffer_completeness_cache() const {
    return framebuffer_completeness_cache_.get();
  }

  bool bind_generates_resource() {
    return bind_generates_resource_;
  }

  uint32_t max_vertex_attribs() const { return max_vertex_attribs_; }

  uint32_t max_texture_units() const { return max_texture_units_; }

  uint32_t max_texture_image_units() const { return max_texture_image_units_; }

  uint32_t max_vertex_texture_image_units() const {
    return max_vertex_texture_image_units_;
  }

  uint32_t max_fragment_uniform_vectors() const {
    return max_fragment_uniform_vectors_;
  }

  uint32_t max_varying_vectors() const { return max_varying_vectors_; }

  uint32_t max_vertex_uniform_vectors() const {
    return max_vertex_uniform_vectors_;
  }

  uint32_t max_color_attachments() const { return max_color_attachments_; }

  uint32_t max_draw_buffers() const { return max_draw_buffers_; }

  uint32_t max_dual_source_draw_buffers() const {
    return max_dual_source_draw_buffers_;
  }

  uint32_t max_vertex_output_components() const {
    return max_vertex_output_components_;
  }

  uint32_t max_fragment_input_components() const {
    return max_fragment_input_components_;
  }

  int32_t min_program_texel_offset() const { return min_program_texel_offset_; }

  int32_t max_program_texel_offset() const { return max_program_texel_offset_; }

  FeatureInfo* feature_info() {
    return feature_info_.get();
  }

  const GpuPreferences& gpu_preferences() const {
    return gpu_preferences_;
  }

  BufferManager* buffer_manager() const {
    return buffer_manager_.get();
  }

  FramebufferManager* framebuffer_manager() const {
    return framebuffer_manager_.get();
  }

  RenderbufferManager* renderbuffer_manager() const {
    return renderbuffer_manager_.get();
  }

  ValuebufferManager* valuebuffer_manager() const {
    return valuebuffer_manager_.get();
  }

  ValueStateMap* pending_valuebuffer_state() const {
    return pending_valuebuffer_state_.get();
  }

  TextureManager* texture_manager() const {
    return texture_manager_.get();
  }

  PathManager* path_manager() const { return path_manager_.get(); }

  ProgramManager* program_manager() const {
    return program_manager_.get();
  }

  bool has_program_cache() const {
    return program_cache_ != NULL;
  }

  void set_program_cache(ProgramCache* program_cache) {
    program_cache_ = program_cache;
  }

  ShaderManager* shader_manager() const {
    return shader_manager_.get();
  }

  TransferBufferManager* transfer_buffer_manager() const {
    return transfer_buffer_manager_.get();
  }

  SamplerManager* sampler_manager() const {
    return sampler_manager_.get();
  }

  uint32_t GetMemRepresented() const;

  // Loses all the context associated with this group.
  void LoseContexts(error::ContextLostReason reason);

  bool GetBufferServiceId(GLuint client_id, GLuint* service_id) const;

  void AddTransformFeedbackId(GLuint client_id, GLuint service_id) {
    transformfeedbacks_id_map_[client_id] = service_id;
  }

  bool GetTransformFeedbackServiceId(
      GLuint client_id, GLuint* service_id) const {
    if (client_id == 0) {
      // Default one.
      if (service_id)
        *service_id = 0;
      return true;
    }
    base::hash_map<GLuint, GLuint>::const_iterator iter =
        transformfeedbacks_id_map_.find(client_id);
    if (iter == transformfeedbacks_id_map_.end())
      return false;
    if (service_id)
      *service_id = iter->second;
    return true;
  }

  void RemoveTransformFeedbackId(GLuint client_id) {
    transformfeedbacks_id_map_.erase(client_id);
  }

  void AddSyncId(GLuint client_id, GLsync service_id) {
    syncs_id_map_[client_id] = service_id;
  }

  bool GetSyncServiceId(GLuint client_id, GLsync* service_id) const {
    base::hash_map<GLuint, GLsync>::const_iterator iter =
        syncs_id_map_.find(client_id);
    if (iter == syncs_id_map_.end())
      return false;
    if (service_id)
      *service_id = iter->second;
    return true;
  }

  void RemoveSyncId(GLuint client_id) {
    syncs_id_map_.erase(client_id);
  }

 private:
  friend class base::RefCounted<ContextGroup>;
  ~ContextGroup();

  bool CheckGLFeature(GLint min_required, GLint* v);
  bool CheckGLFeatureU(GLint min_required, uint32_t* v);
  bool QueryGLFeature(GLenum pname, GLint min_required, GLint* v);
  bool QueryGLFeatureU(GLenum pname, GLint min_required, uint32_t* v);
  bool HaveContexts();

  const GpuPreferences& gpu_preferences_;
  scoped_refptr<MailboxManager> mailbox_manager_;
  scoped_refptr<MemoryTracker> memory_tracker_;
  scoped_refptr<ShaderTranslatorCache> shader_translator_cache_;
  scoped_refptr<FramebufferCompletenessCache> framebuffer_completeness_cache_;
  scoped_refptr<TransferBufferManager> transfer_buffer_manager_;
  scoped_refptr<SubscriptionRefSet> subscription_ref_set_;
  scoped_refptr<ValueStateMap> pending_valuebuffer_state_;

  bool enforce_gl_minimums_;
  bool bind_generates_resource_;

  uint32_t max_vertex_attribs_;
  uint32_t max_texture_units_;
  uint32_t max_texture_image_units_;
  uint32_t max_vertex_texture_image_units_;
  uint32_t max_fragment_uniform_vectors_;
  uint32_t max_varying_vectors_;
  uint32_t max_vertex_uniform_vectors_;
  uint32_t max_color_attachments_;
  uint32_t max_draw_buffers_;
  uint32_t max_dual_source_draw_buffers_;

  uint32_t max_vertex_output_components_;
  uint32_t max_fragment_input_components_;
  int32_t min_program_texel_offset_;
  int32_t max_program_texel_offset_;

  ProgramCache* program_cache_;

  scoped_ptr<BufferManager> buffer_manager_;

  scoped_ptr<FramebufferManager> framebuffer_manager_;

  scoped_ptr<RenderbufferManager> renderbuffer_manager_;

  scoped_ptr<TextureManager> texture_manager_;

  scoped_ptr<PathManager> path_manager_;

  scoped_ptr<ProgramManager> program_manager_;

  scoped_ptr<ShaderManager> shader_manager_;

  scoped_ptr<SamplerManager> sampler_manager_;

  scoped_ptr<ValuebufferManager> valuebuffer_manager_;

  scoped_refptr<FeatureInfo> feature_info_;

  std::vector<base::WeakPtr<gles2::GLES2Decoder> > decoders_;

  // Mappings from client side IDs to service side IDs.
  base::hash_map<GLuint, GLuint> transformfeedbacks_id_map_;
  base::hash_map<GLuint, GLsync> syncs_id_map_;

  DISALLOW_COPY_AND_ASSIGN(ContextGroup);
};

}  // namespace gles2
}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_CONTEXT_GROUP_H_
