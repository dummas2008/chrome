// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.content.Context;
import android.os.ConditionVariable;

import org.chromium.base.Log;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;

/**
 * Wrapper class to start a Quic test server.
 */
@JNINamespace("cronet")
public final class QuicTestServer {
    private static final ConditionVariable sBlock = new ConditionVariable();
    private static final String TAG = "cr.QuicTestServer";

    private static final String CERT_USED = "quic_test.example.com.crt";
    private static final String KEY_USED = "quic_test.example.com.key";
    private static final String[] CERTS_USED = {CERT_USED};

    public static void startQuicTestServer(Context context) {
        TestFilesInstaller.installIfNeeded(context);
        nativeStartQuicTestServer(TestFilesInstaller.getInstalledPath(context));
        sBlock.block();
    }

    public static void shutdownQuicTestServer() {
        nativeShutdownQuicTestServer();
        sBlock.close();
    }

    public static String getServerURL() {
        return "https://" + getServerHost() + ":" + getServerPort();
    }

    public static String getServerHost() {
        return nativeGetServerHost();
    }

    public static int getServerPort() {
        return nativeGetServerPort();
    }

    public static final String getServerCert() {
        return CERT_USED;
    }

    public static final String getServerCertKey() {
        return KEY_USED;
    }

    public static long createMockCertVerifier() {
        return MockCertVerifier.createMockCertVerifier(CERTS_USED);
    }

    @CalledByNative
    private static void onServerStarted() {
        Log.i(TAG, "Quic server started.");
        sBlock.open();
    }

    private static native void nativeStartQuicTestServer(String filePath);
    private static native void nativeShutdownQuicTestServer();
    private static native String nativeGetServerHost();
    private static native int nativeGetServerPort();
}
