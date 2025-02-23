// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines all the "cc" command-line switches.

#ifndef CC_BASE_SWITCHES_H_
#define CC_BASE_SWITCHES_H_

#include "cc/base/cc_export.h"

// Since cc is used from the render process, anything that goes here also needs
// to be added to render_process_host_impl.cc.

namespace cc {
namespace switches {

// Switches for the renderer compositor only.
CC_EXPORT extern const char kDisableThreadedAnimation[];
CC_EXPORT extern const char kDisableCachedPictureRaster[];
CC_EXPORT extern const char kDisableCompositedAntialiasing[];
CC_EXPORT extern const char kDisableMainFrameBeforeActivation[];
CC_EXPORT extern const char kEnableMainFrameBeforeActivation[];
CC_EXPORT extern const char kTopControlsHideThreshold[];
CC_EXPORT extern const char kTopControlsShowThreshold[];
CC_EXPORT extern const char kSlowDownRasterScaleFactor[];
CC_EXPORT extern const char kStrictLayerPropertyChangeChecking[];
CC_EXPORT extern const char kEnableTileCompression[];

// Switches for both the renderer and ui compositors.
CC_EXPORT extern const char kEnableBeginFrameScheduling[];
CC_EXPORT extern const char kEnableGpuBenchmarking[];

// Debug visualizations.
CC_EXPORT extern const char kShowCompositedLayerBorders[];
CC_EXPORT extern const char kUIShowCompositedLayerBorders[];
CC_EXPORT extern const char kShowFPSCounter[];
CC_EXPORT extern const char kUIShowFPSCounter[];
CC_EXPORT extern const char kShowLayerAnimationBounds[];
CC_EXPORT extern const char kUIShowLayerAnimationBounds[];
CC_EXPORT extern const char kShowPropertyChangedRects[];
CC_EXPORT extern const char kUIShowPropertyChangedRects[];
CC_EXPORT extern const char kShowSurfaceDamageRects[];
CC_EXPORT extern const char kUIShowSurfaceDamageRects[];
CC_EXPORT extern const char kShowScreenSpaceRects[];
CC_EXPORT extern const char kUIShowScreenSpaceRects[];
CC_EXPORT extern const char kShowReplicaScreenSpaceRects[];
CC_EXPORT extern const char kUIShowReplicaScreenSpaceRects[];
CC_EXPORT extern const char kEnableLayerLists[];
CC_EXPORT extern const char kUIEnableLayerLists[];

// Unit test related.
CC_EXPORT extern const char kCCLayerTreeTestNoTimeout[];
CC_EXPORT extern const char kCCRebaselinePixeltests[];

}  // namespace switches
}  // namespace cc

#endif  // CC_BASE_SWITCHES_H_
