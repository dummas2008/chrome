// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.util;

import android.annotation.SuppressLint;
import android.annotation.TargetApi;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Build;
import android.os.Bundle;
import android.os.StrictMode;
import android.os.UserManager;
import android.speech.RecognizerIntent;
import android.text.TextUtils;

import org.chromium.base.ApplicationStatus;
import org.chromium.base.CommandLine;
import org.chromium.base.FieldTrialList;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.VisibleForTesting;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.ChromeVersionInfo;
import org.chromium.chrome.browser.preferences.ChromePreferenceManager;
import org.chromium.chrome.browser.preferences.DocumentModeManager;
import org.chromium.sync.signin.AccountManagerHelper;
import org.chromium.ui.base.DeviceFormFactor;

import java.util.List;

/**
 * A utility {@code class} meant to help determine whether or not certain features are supported by
 * this device.
 */
public class FeatureUtilities {
    private static final String TAG = "FeatureUtilities";
    private static final String HERB_EXPERIMENT_NAME = "TabManagementExperiment";
    private static final String HERB_EXPERIMENT_FLAVOR_PARAM = "type";

    private static Boolean sHasGoogleAccountAuthenticator;
    private static Boolean sHasRecognitionIntentHandler;
    private static Boolean sDocumentModeDisabled;

    private static String sCachedHerbFlavor;
    private static boolean sIsHerbFlavorCached;

    /** Used to track if cached command line flags should be refreshed. */
    private static CommandLine.ResetListener sResetListener = null;

    /**
     * Determines whether or not the {@link RecognizerIntent#ACTION_WEB_SEARCH} {@link Intent}
     * is handled by any {@link android.app.Activity}s in the system.  The result will be cached for
     * future calls.  Passing {@code false} to {@code useCachedValue} will force it to re-query any
     * {@link android.app.Activity}s that can process the {@link Intent}.
     * @param context        The {@link Context} to use to check to see if the {@link Intent} will
     *                       be handled.
     * @param useCachedValue Whether or not to use the cached value from a previous result.
     * @return {@code true} if recognition is supported.  {@code false} otherwise.
     */
    public static boolean isRecognitionIntentPresent(Context context, boolean useCachedValue) {
        ThreadUtils.assertOnUiThread();
        if (sHasRecognitionIntentHandler == null || !useCachedValue) {
            PackageManager pm = context.getPackageManager();
            List<ResolveInfo> activities = pm.queryIntentActivities(
                    new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH), 0);
            sHasRecognitionIntentHandler = activities.size() > 0;
        }

        return sHasRecognitionIntentHandler;
    }

    /**
     * Determines whether or not the user has a Google account (so we can sync) or can add one.
     * @param context The {@link Context} that we should check accounts under.
     * @return Whether or not sync is allowed on this device.
     */
    public static boolean canAllowSync(Context context) {
        return (hasGoogleAccountAuthenticator(context) && hasSyncPermissions(context))
                || hasGoogleAccounts(context);
    }

    @VisibleForTesting
    static boolean hasGoogleAccountAuthenticator(Context context) {
        if (sHasGoogleAccountAuthenticator == null) {
            AccountManagerHelper accountHelper = AccountManagerHelper.get(context);
            sHasGoogleAccountAuthenticator = accountHelper.hasGoogleAccountAuthenticator();
        }
        return sHasGoogleAccountAuthenticator;
    }

    @VisibleForTesting
    static boolean hasGoogleAccounts(Context context) {
        return AccountManagerHelper.get(context).hasGoogleAccounts();
    }

    @SuppressLint("InlinedApi")
    @TargetApi(Build.VERSION_CODES.JELLY_BEAN_MR2)
    private static boolean hasSyncPermissions(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.JELLY_BEAN_MR2) return true;

        UserManager manager = (UserManager) context.getSystemService(Context.USER_SERVICE);
        Bundle userRestrictions = manager.getUserRestrictions();
        return !userRestrictions.getBoolean(UserManager.DISALLOW_MODIFY_ACCOUNTS, false);
    }

    /**
     * Check whether Chrome should be running on document mode.
     * @param context The context to use for checking configuration.
     * @return Whether Chrome should be running on document mode.
     */
    public static boolean isDocumentMode(Context context) {
        if (sDocumentModeDisabled == null && CommandLine.isInitialized()) {
            initResetListener();
            sDocumentModeDisabled = CommandLine.getInstance().hasSwitch(
                    ChromeSwitches.DISABLE_DOCUMENT_MODE);
        }
        return isDocumentModeEligible(context)
                && !DocumentModeManager.getInstance(context).isOptedOutOfDocumentMode()
                && (sDocumentModeDisabled == null || !sDocumentModeDisabled.booleanValue());
    }

    /**
     * Whether the device could possibly run in Document mode (may return true even if the document
     * mode is turned off).
     *
     * This function can't be changed to return false (even if document mode is deleted) because we
     * need to know whether a user needs to be migrated away.
     *
     * @param context The context to use for checking configuration.
     * @return Whether the device could possibly run in Document mode.
     */
    public static boolean isDocumentModeEligible(Context context) {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP
                && !DeviceFormFactor.isTablet(context);
    }

    /**
     * Records the current document mode state with native-side feature utilities.
     * @param enabled Whether the document mode is enabled.
     */
    public static void setDocumentModeEnabled(boolean enabled) {
        nativeSetDocumentModeEnabled(enabled);
    }

    /**
     * Records the current custom tab visibility state with native-side feature utilities.
     * @param visible Whether a custom tab is visible.
     */
    public static void setCustomTabVisible(boolean visible) {
        nativeSetCustomTabVisible(visible);
    }

    /**
     * Check whether tab switching is enabled for the current context.
     * Note that this may return false if native library is not yet ready.
     * @param context The context
     * @return Whether tab switching is enabled for the current context.
     */
    public static boolean isTabSwitchingEnabled(Context context) {
        return !isDocumentMode(context) || isTabSwitchingEnabledInDocumentModeInternal();
    }

    /**
     * Check whether tab switching is enabled in document mode.
     * Note that this may return false if native library is not yet ready.
     * @return Whether tab switching is enabled in document mode.
     */
    public static boolean isTabSwitchingEnabledInDocumentMode(Context context) {
        return isDocumentMode(context) && isTabSwitchingEnabledInDocumentModeInternal();
    }

    private static boolean isTabSwitchingEnabledInDocumentModeInternal() {
        return CommandLine.getInstance().hasSwitch(
                ChromeSwitches.ENABLE_TAB_SWITCHER_IN_DOCUMENT_MODE);
    }

    private static void initResetListener() {
        if (sResetListener != null) return;

        sResetListener = new CommandLine.ResetListener() {
            @Override
            public void onCommandLineReset() {
                sDocumentModeDisabled = null;
            }
        };
        CommandLine.addResetListener(sResetListener);
    }

    private static boolean isHerbDisallowed(Context context) {
        return isDocumentMode(context) || ChromeVersionInfo.isStableBuild()
                || ChromeVersionInfo.isBetaBuild() || DeviceFormFactor.isTablet(context);
    }

    /**
     * @return Which flavor of Herb is being tested.  See {@link ChromeSwitches#HERB_FLAVOR_ANISE}
     *         and its related switches.
     */
    public static String getHerbFlavor() {
        Context context = ApplicationStatus.getApplicationContext();
        if (isHerbDisallowed(context)) return ChromeSwitches.HERB_FLAVOR_DISABLED;

        if (!sIsHerbFlavorCached) {
            sCachedHerbFlavor = null;

            // Allowing disk access for preferences while prototyping.
            StrictMode.ThreadPolicy oldPolicy = StrictMode.allowThreadDiskReads();
            try {
                sCachedHerbFlavor =
                        ChromePreferenceManager.getInstance(context).getCachedHerbFlavor();
            } finally {
                StrictMode.setThreadPolicy(oldPolicy);
            }

            sIsHerbFlavorCached = true;
            Log.d(TAG, "Retrieved cached Herb flavor: " + sCachedHerbFlavor);
        }

        return sCachedHerbFlavor;
    }

    /**
     * Caches flags that must take effect on startup but are set via native code.
     */
    public static void cacheNativeFlags() {
        cacheHerbFlavor();
    }

    /**
     * Caches which flavor of Herb the user prefers from native.
     */
    private static void cacheHerbFlavor() {
        Context context = ApplicationStatus.getApplicationContext();
        if (isHerbDisallowed(context)) return;

        String oldFlavor = getHerbFlavor();

        // Check the experiment value before the command line to put the user in the correct group.
        // The first clause does the null checks so so we can freely use the startsWith() function.
        String newFlavor = FieldTrialList.findFullName(HERB_EXPERIMENT_NAME);
        Log.d(TAG, "Experiment flavor: " + newFlavor);
        if (TextUtils.isEmpty(newFlavor)
                || newFlavor.startsWith(ChromeSwitches.HERB_FLAVOR_CONTROL)
                || newFlavor.startsWith(ChromeSwitches.HERB_FLAVOR_DEFAULT)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_DISABLED;
        } else if (newFlavor.startsWith(ChromeSwitches.HERB_FLAVOR_ANISE)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_ANISE;
        } else if (newFlavor.startsWith(ChromeSwitches.HERB_FLAVOR_BASIL)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_BASIL;
        } else if (newFlavor.startsWith(ChromeSwitches.HERB_FLAVOR_CHIVE)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_CHIVE;
        } else if (newFlavor.startsWith(ChromeSwitches.HERB_FLAVOR_DILL)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_DILL;
        } else if (newFlavor.startsWith(ChromeSwitches.HERB_FLAVOR_ELDERBERRY)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_ELDERBERRY;
        }

        CommandLine instance = CommandLine.getInstance();
        if (instance.hasSwitch(ChromeSwitches.HERB_FLAVOR_DISABLED_SWITCH)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_DISABLED;
        } else if (instance.hasSwitch(ChromeSwitches.HERB_FLAVOR_ANISE_SWITCH)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_ANISE;
        } else if (instance.hasSwitch(ChromeSwitches.HERB_FLAVOR_BASIL_SWITCH)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_BASIL;
        } else if (instance.hasSwitch(ChromeSwitches.HERB_FLAVOR_CHIVE_SWITCH)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_CHIVE;
        } else if (instance.hasSwitch(ChromeSwitches.HERB_FLAVOR_DILL_SWITCH)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_DILL;
        } else if (instance.hasSwitch(ChromeSwitches.HERB_FLAVOR_ELDERBERRY_SWITCH)) {
            newFlavor = ChromeSwitches.HERB_FLAVOR_ELDERBERRY;
        }

        Log.d(TAG, "Caching flavor: " + newFlavor);
        sCachedHerbFlavor = newFlavor;

        if (!TextUtils.equals(oldFlavor, newFlavor)) {
            ChromePreferenceManager.getInstance(context).setCachedHerbFlavor(newFlavor);
        }
    }

    private static native void nativeSetDocumentModeEnabled(boolean enabled);
    private static native void nativeSetCustomTabVisible(boolean visible);
    public static native void nativeSetSqlMmapDisabledByDefault();
}
