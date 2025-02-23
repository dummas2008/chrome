// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/media/midi_permission_infobar_delegate_android.h"

#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/grit/generated_resources.h"
#include "components/infobars/core/infobar.h"
#include "components/url_formatter/elide_url.h"
#include "grit/theme_resources.h"
#include "ui/base/l10n/l10n_util.h"

// static
infobars::InfoBar* MidiPermissionInfoBarDelegateAndroid::Create(
    InfoBarService* infobar_service,
    const GURL& requesting_frame,
    const PermissionSetCallback& callback) {
  return infobar_service->AddInfoBar(infobar_service->CreateConfirmInfoBar(
      std::unique_ptr<ConfirmInfoBarDelegate>(
          new MidiPermissionInfoBarDelegateAndroid(requesting_frame,
                                                   callback))));
}

MidiPermissionInfoBarDelegateAndroid::MidiPermissionInfoBarDelegateAndroid(
    const GURL& requesting_frame,
    const PermissionSetCallback& callback)
    : PermissionInfobarDelegate(requesting_frame,
                                content::PermissionType::MIDI_SYSEX,
                                CONTENT_SETTINGS_TYPE_MIDI_SYSEX,
                                callback),
      requesting_frame_(requesting_frame) {}

MidiPermissionInfoBarDelegateAndroid::~MidiPermissionInfoBarDelegateAndroid() {}

infobars::InfoBarDelegate::InfoBarIdentifier
MidiPermissionInfoBarDelegateAndroid::GetIdentifier() const {
  return MIDI_PERMISSION_INFOBAR_DELEGATE_ANDROID;
}

int MidiPermissionInfoBarDelegateAndroid::GetIconId() const {
  return IDR_INFOBAR_MIDI;
}

base::string16 MidiPermissionInfoBarDelegateAndroid::GetMessageText() const {
  return l10n_util::GetStringFUTF16(
      IDS_MIDI_SYSEX_INFOBAR_QUESTION,
      url_formatter::FormatUrlForSecurityDisplay(
          requesting_frame_.GetOrigin()));
}
