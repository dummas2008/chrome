// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blimp/client/app/android/blimp_client_session_android.h"

#include <string>

#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/thread_task_runner_handle.h"
#include "blimp/client/app/user_agent.h"
#include "blimp/client/feature/settings_feature.h"
#include "blimp/client/feature/tab_control_feature.h"
#include "blimp/client/session/assignment_source.h"
#include "jni/BlimpClientSession_jni.h"
#include "net/base/net_errors.h"

namespace blimp {
namespace client {
namespace {
const int kDummyTabId = 0;

GURL CreateAssignerGURL(const std::string& assigner_url) {
  GURL parsed_url(assigner_url);
  CHECK(parsed_url.is_valid());
  return parsed_url;
}

}  // namespace

static jlong Init(JNIEnv* env,
                  const JavaParamRef<jobject>& jobj,
                  const base::android::JavaParamRef<jstring>& jassigner_url) {
  return reinterpret_cast<intptr_t>(
      new BlimpClientSessionAndroid(env, jobj, jassigner_url));
}

// static
bool BlimpClientSessionAndroid::RegisterJni(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

// static
BlimpClientSessionAndroid* BlimpClientSessionAndroid::FromJavaObject(
    JNIEnv* env,
    jobject jobj) {
  return reinterpret_cast<BlimpClientSessionAndroid*>(
      Java_BlimpClientSession_getNativePtr(env, jobj));
}

BlimpClientSessionAndroid::BlimpClientSessionAndroid(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jobj,
    const base::android::JavaParamRef<jstring>& jassigner_url)
    : BlimpClientSession(CreateAssignerGURL(
          base::android::ConvertJavaStringToUTF8(jassigner_url))) {
  java_obj_.Reset(env, jobj);

  // Create a single tab's WebContents.
  // TODO(kmarshall): Remove this once we add tab-literacy to Blimp.
  GetTabControlFeature()->CreateTab(kDummyTabId);
}

void BlimpClientSessionAndroid::Connect(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jobj,
    const base::android::JavaParamRef<jstring>& jclient_auth_token) {
  std::string client_auth_token;
  if (jclient_auth_token.obj()) {
    client_auth_token =
        base::android::ConvertJavaStringToUTF8(env, jclient_auth_token);
  }

  BlimpClientSession::Connect(client_auth_token);
}

BlimpClientSessionAndroid::~BlimpClientSessionAndroid() {}

void BlimpClientSessionAndroid::OnConnected() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_BlimpClientSession_onConnected(env, java_obj_.obj());

  GetSettingsFeature()->SendUserAgentOSVersionInfo(
      GetOSVersionInfoForUserAgent());
}

void BlimpClientSessionAndroid::OnDisconnected(int result) {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_BlimpClientSession_onDisconnected(
      env, java_obj_.obj(), base::android::ConvertUTF8ToJavaString(
          env, net::ErrorToShortString(result)).obj());
}

void BlimpClientSessionAndroid::Destroy(JNIEnv* env,
                                        const JavaParamRef<jobject>& jobj) {
  delete this;
}

void BlimpClientSessionAndroid::OnAssignmentConnectionAttempted(
    AssignmentSource::Result result) {
  // Notify the front end of the assignment result.
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_BlimpClientSession_onAssignmentReceived(env, java_obj_.obj(),
                                               static_cast<jint>(result));

  BlimpClientSession::OnAssignmentConnectionAttempted(result);
}

}  // namespace client
}  // namespace blimp
