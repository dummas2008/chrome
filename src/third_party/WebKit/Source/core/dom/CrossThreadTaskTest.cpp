// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/dom/CrossThreadTask.h"

#include "platform/heap/Handle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class GCObject : public GarbageCollectedFinalized<GCObject> {
public:
    static int s_counter;
    GCObject() { ++s_counter; }
    ~GCObject() { --s_counter; }
    DEFINE_INLINE_TRACE() { }
    void run(GCObject*) { }
};

int GCObject::s_counter = 0;

static void functionWithGarbageCollected(GCObject*)
{
}

static void functionWithExecutionContext(GCObject*, ExecutionContext*)
{
}

class CrossThreadTaskTest : public testing::Test {
protected:
    void SetUp() override
    {
        GCObject::s_counter = 0;
    }
    void TearDown() override
    {
        Heap::collectGarbage(BlinkGC::NoHeapPointersOnStack, BlinkGC::GCWithSweep, BlinkGC::ForcedGC);
        ASSERT_EQ(0, GCObject::s_counter);
    }
};

TEST_F(CrossThreadTaskTest, CreateForGarbageCollectedMethod)
{
    OwnPtr<ExecutionContextTask> task1 = createCrossThreadTask(&GCObject::run, new GCObject, new GCObject);
    OwnPtr<ExecutionContextTask> task2 = createCrossThreadTask(&GCObject::run, new GCObject, new GCObject);
    Heap::collectGarbage(BlinkGC::NoHeapPointersOnStack, BlinkGC::GCWithSweep, BlinkGC::ForcedGC);
    EXPECT_EQ(4, GCObject::s_counter);
}

TEST_F(CrossThreadTaskTest, CreateForFunctionWithGarbageCollected)
{
    OwnPtr<ExecutionContextTask> task1 = createCrossThreadTask(&functionWithGarbageCollected, new GCObject);
    OwnPtr<ExecutionContextTask> task2 = createCrossThreadTask(&functionWithGarbageCollected, new GCObject);
    Heap::collectGarbage(BlinkGC::NoHeapPointersOnStack, BlinkGC::GCWithSweep, BlinkGC::ForcedGC);
    EXPECT_EQ(2, GCObject::s_counter);
}

TEST_F(CrossThreadTaskTest, CreateForFunctionWithExecutionContext)
{
    OwnPtr<ExecutionContextTask> task1 = createCrossThreadTask(&functionWithExecutionContext, new GCObject);
    OwnPtr<ExecutionContextTask> task2 = createCrossThreadTask(&functionWithExecutionContext, new GCObject);
    Heap::collectGarbage(BlinkGC::NoHeapPointersOnStack, BlinkGC::GCWithSweep, BlinkGC::ForcedGC);
    EXPECT_EQ(2, GCObject::s_counter);
}

} // namespace blink
