// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CustomElementSyncMicrotaskQueue_h
#define CustomElementSyncMicrotaskQueue_h

#include "core/dom/custom/CustomElementMicrotaskQueueBase.h"

namespace blink {

class CustomElementSyncMicrotaskQueue : public CustomElementMicrotaskQueueBase {
public:
    static CustomElementSyncMicrotaskQueue* create() { return new CustomElementSyncMicrotaskQueue(); }

    void enqueue(CustomElementMicrotaskStep*);

private:
    CustomElementSyncMicrotaskQueue() { }
    virtual void doDispatch();
};

} // namespace blink

#endif // CustomElementSyncMicrotaskQueue_h
