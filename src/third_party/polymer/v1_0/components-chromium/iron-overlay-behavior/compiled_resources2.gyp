# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NOTE: Created with generate_compiled_resources_gyp.py, please do not edit.
{
  'targets': [
    {
      'target_name': 'iron-overlay-backdrop-extracted',
      'dependencies': [
        'iron-overlay-manager-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'iron-overlay-behavior-extracted',
      'dependencies': [
        '../iron-a11y-keys-behavior/compiled_resources2.gyp:iron-a11y-keys-behavior-extracted',
        '../iron-fit-behavior/compiled_resources2.gyp:iron-fit-behavior-extracted',
        '../iron-resizable-behavior/compiled_resources2.gyp:iron-resizable-behavior-extracted',
        'iron-overlay-backdrop-extracted',
        'iron-overlay-manager-extracted',
      ],
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
    {
      'target_name': 'iron-overlay-manager-extracted',
      'includes': ['../../../../closure_compiler/compile_js2.gypi'],
    },
  ],
}
