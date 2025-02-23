// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.invalidation;

import android.accounts.Account;
import android.app.Activity;
import android.app.Application;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.SyncResult;
import android.os.Bundle;
import android.test.suitebuilder.annotation.MediumTest;

import org.chromium.base.ApplicationStatus;
import org.chromium.base.CommandLine;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.ScalableTimeout;
import org.chromium.chrome.browser.ChromeActivity;
import org.chromium.chrome.test.ChromeActivityTestCaseBase;
import org.chromium.components.invalidation.PendingInvalidation;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.sync.AndroidSyncSettings;
import org.chromium.sync.signin.AccountManagerHelper;

/**
 * Tests for ChromiumSyncAdapter.
 */
public class ChromiumSyncAdapterTest extends ChromeActivityTestCaseBase<ChromeActivity> {
    private static final Account TEST_ACCOUNT =
            AccountManagerHelper.createAccountFromName("test@gmail.com");
    private static final long WAIT_FOR_LAUNCHER_MS = ScalableTimeout.scaleTimeout(10 * 1000);
    private static final long POLL_INTERVAL_MS = 100;

    private TestChromiumSyncAdapter mSyncAdapter;

    private static class TestChromiumSyncAdapter extends ChromiumSyncAdapter {
        private boolean mInvalidated;
        private boolean mInvalidatedAllTypes;
        private int mObjectSource;
        private String mObjectId;
        private long mVersion;
        private String mPayload;

        public TestChromiumSyncAdapter(Context context, Application application) {
            super(context, application);
        }

        @Override
        protected boolean useAsyncStartup() {
            return true;
        }

        @Override
        public void notifyInvalidation(
                int objectSource, String objectId, long version, String payload) {
            mObjectSource = objectSource;
            mObjectId = objectId;
            mVersion = version;
            mPayload = payload;
            if (objectId == null) {
                mInvalidatedAllTypes = true;
            } else {
                mInvalidated = true;
            }
        }
    }

    public ChromiumSyncAdapterTest() {
        super(ChromeActivity.class);
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        mSyncAdapter = new TestChromiumSyncAdapter(
                getInstrumentation().getTargetContext(), getActivity().getApplication());
    }

    @Override
    public void startMainActivity() throws InterruptedException {
        startMainActivityOnBlankPage();
    }

    private void performSyncWithBundle(Bundle bundle) {
        mSyncAdapter.onPerformSync(TEST_ACCOUNT, bundle,
                AndroidSyncSettings.getContractAuthority(getActivity()), null, new SyncResult());
    }

    private void sendChromeToBackground(Activity activity) throws InterruptedException {
        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.addCategory(Intent.CATEGORY_HOME);
        activity.startActivity(intent);

        CriteriaHelper.pollInstrumentationThread(new Criteria("Activity should have been resumed") {
            @Override
            public boolean isSatisfied() {
                return !isActivityResumed();
            }
        }, WAIT_FOR_LAUNCHER_MS, POLL_INTERVAL_MS);
    }

    private boolean isActivityResumed() {
        return ApplicationStatus.hasVisibleActivities();
    }

    @MediumTest
    @Feature({"Sync"})
    public void testRequestSyncNoInvalidationData() {
        performSyncWithBundle(new Bundle());
        assertTrue(mSyncAdapter.mInvalidatedAllTypes);
        assertFalse(mSyncAdapter.mInvalidated);
        assertTrue(CommandLine.isInitialized());
    }

    @MediumTest
    @Feature({"Sync"})
    public void testRequestSyncSpecificDataType() {
        String objectId = "objectid_value";
        int objectSource = 61;
        long version = 42L;
        String payload = "payload_value";

        performSyncWithBundle(
                PendingInvalidation.createBundle(objectId, objectSource, version, payload));

        assertFalse(mSyncAdapter.mInvalidatedAllTypes);
        assertTrue(mSyncAdapter.mInvalidated);
        assertEquals(objectSource, mSyncAdapter.mObjectSource);
        assertEquals(objectId, mSyncAdapter.mObjectId);
        assertEquals(version, mSyncAdapter.mVersion);
        assertEquals(payload, mSyncAdapter.mPayload);
        assertTrue(CommandLine.isInitialized());
    }

    @MediumTest
    @Feature({"Sync"})
    public void testRequestSyncWhenChromeInBackground() throws InterruptedException {
        sendChromeToBackground(getActivity());
        performSyncWithBundle(new Bundle());
        assertFalse(mSyncAdapter.mInvalidatedAllTypes);
        assertFalse(mSyncAdapter.mInvalidated);
        assertTrue(CommandLine.isInitialized());
    }

    @MediumTest
    @Feature({"Sync"})
    public void testRequestInitializeSync() throws InterruptedException {
        Bundle extras = new Bundle();
        extras.putBoolean(ContentResolver.SYNC_EXTRAS_INITIALIZE, true);
        performSyncWithBundle(extras);
        assertFalse(mSyncAdapter.mInvalidatedAllTypes);
        assertFalse(mSyncAdapter.mInvalidated);
    }
}
