// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_MUS_AURA_INIT_H_
#define UI_VIEWS_MUS_AURA_INIT_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "build/build_config.h"
#include "skia/ext/refptr.h"
#include "ui/views/mus/mus_export.h"

namespace aura {
class Env;
}

namespace font_service {
class FontLoader;
}

namespace mojo {
class Connector;
}

namespace views {
class ViewsDelegate;

// Sets up necessary state for aura when run with the viewmanager.
// |resource_file| is the path to the apk file containing the resources.
class VIEWS_MUS_EXPORT AuraInit {
 public:
  AuraInit(mojo::Connector* connector, const std::string& resource_file);
  ~AuraInit();

 private:
  void InitializeResources(mojo::Connector* connector);

#if defined(OS_LINUX) && !defined(OS_ANDROID)
  skia::RefPtr<font_service::FontLoader> font_loader_;
#endif

  const std::string resource_file_;

  std::unique_ptr<aura::Env> env_;
  scoped_ptr<ViewsDelegate> views_delegate_;

  DISALLOW_COPY_AND_ASSIGN(AuraInit);
};

}  // namespace views

#endif  // UI_VIEWS_MUS_AURA_INIT_H_
