// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromoting.host.jni;

import android.content.Context;

import org.chromium.base.ContextUtils;
import org.chromium.base.annotations.JNINamespace;

/**
 * Class to allow Java code to access the native C++ implementation of the Host process. This class
 * controls the lifetime of the corresponding C++ object.
 */
@JNINamespace("remoting")
public class Host {
    // Pointer to the C++ object, cast to a |long|.
    private long mNativeJniHost;

    private static boolean sLoaded;

    // Called once from the main Activity. Loads and initializes the native
    // code.
    public static void loadLibrary(Context context) {
        if (sLoaded) return;

        System.loadLibrary("remoting_host_jni");
        ContextUtils.initApplicationContext(context.getApplicationContext());
        sLoaded = true;
    }

    public Host() {
        mNativeJniHost = nativeInit();
    }

    private native long nativeInit();

    public void destroy() {
        nativeDestroy(mNativeJniHost);
    }

    private native void nativeDestroy(long nativeJniHost);
}
