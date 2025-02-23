// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_COMPOSITOR_LAYER_TAB_LAYER_H_
#define CHROME_BROWSER_ANDROID_COMPOSITOR_LAYER_TAB_LAYER_H_

#include <memory>

#include "base/macros.h"
#include "chrome/browser/android/compositor/layer/layer.h"

namespace cc {
class ImageLayer;
class Layer;
class NinePatchLayer;
class SolidColorLayer;
class UIResourceLayer;
}

namespace gfx {
class Size;
}

namespace ui {
class ResourceManager;
}

class SkBitmap;

namespace chrome {
namespace android {

class ContentLayer;
class DecorationTitle;
class LayerTitleCache;
class TabContentManager;
class ToolbarLayer;

// Sub layer tree representation of a tab.  A TabLayer is not tied to
// specific tab. To specialize it call CustomizeForId() and SetProperties().
class TabLayer : public Layer {
 public:
  static scoped_refptr<TabLayer> Create(bool incognito,
                                        ui::ResourceManager* resource_manager,
                                        LayerTitleCache* layer_title_cache,
                                        TabContentManager* tab_content_manager);

  void SetProperties(int id,
                     bool can_use_live_layer,
                     int toolbar_resource_id,
                     int close_button_resource_id,
                     int shadow_resource_id,
                     int contour_resource_id,
                     int back_logo_resource_id,
                     int border_resource_id,
                     int border_inner_shadow_resource_id,
                     int default_background_color,
                     int back_logo_color,
                     bool is_portrait,
                     float x,
                     float y,
                     float width,
                     float height,
                     float shadow_x,
                     float shadow_y,
                     float shadow_width,
                     float shadow_height,
                     float pivot_x,
                     float pivot_y,
                     float rotation_x,
                     float rotation_y,
                     float alpha,
                     float border_alpha,
                     float border_inner_shadow_alpha,
                     float contour_alpha,
                     float shadow_alpha,
                     float close_alpha,
                     float border_scale,
                     float saturation,
                     float brightness,
                     float close_btn_width,
                     float static_to_view_blend,
                     float content_width,
                     float content_height,
                     float view_width,
                     float view_height,
                     bool show_toolbar,
                     int toolbar_background_color,
                     bool anonymize_toolbar,
                     int toolbar_textbox_resource_id,
                     int toolbar_textbox_background_color,
                     float toolbar_textbox_alpha,
                     float toolbar_alpha,
                     float toolbar_y_offset,
                     float side_border_scale,
                     bool attach_content,
                     bool inset_border);

  bool is_incognito() const { return incognito_; }

  scoped_refptr<cc::Layer> layer() override;

 protected:
  TabLayer(bool incognito,
           ui::ResourceManager* resource_manager,
           LayerTitleCache* layer_title_cache,
           TabContentManager* tab_content_manager);
  ~TabLayer() override;

 private:
  void SetTitle(DecorationTitle* title);

  const bool incognito_;
  ui::ResourceManager* resource_manager_;
  LayerTitleCache* layer_title_cache_;

  // [layer]-+-[toolbar]
  //         +-[close button]
  //         +-[title]
  //         +-[front border]
  //         +-[content]
  //         +-[back_logo]
  //         +-[padding]
  //         +-[contour_shadow]
  //         +-[shadow]
  //
  // [back logo]
  scoped_refptr<cc::Layer> layer_;
  scoped_refptr<ToolbarLayer> toolbar_layer_;
  scoped_refptr<cc::Layer> title_;
  scoped_refptr<ContentLayer> content_;
  scoped_refptr<cc::SolidColorLayer> padding_;
  scoped_refptr<cc::UIResourceLayer> close_button_;
  scoped_refptr<cc::NinePatchLayer> front_border_;
  scoped_refptr<cc::NinePatchLayer> front_border_inner_shadow_;
  scoped_refptr<cc::NinePatchLayer> contour_shadow_;
  scoped_refptr<cc::NinePatchLayer> shadow_;
  scoped_refptr<cc::UIResourceLayer> back_logo_;
  float brightness_;

  DISALLOW_COPY_AND_ASSIGN(TabLayer);
};

}  //  namespace android
}  //  namespace chrome

#endif  // CHROME_BROWSER_ANDROID_COMPOSITOR_LAYER_TAB_LAYER_H_
