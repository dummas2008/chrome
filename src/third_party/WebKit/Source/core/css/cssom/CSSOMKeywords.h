// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CSSOMKeywords_h
#define CSSOMKeywords_h

#include "core/CSSPropertyNames.h"

namespace blink {

class KeywordValue;

class CSSOMKeywords {
    STATIC_ONLY(CSSOMKeywords);

public:
    static bool validKeywordForProperty(CSSPropertyID, const KeywordValue&);
};

} // namespace blink

#endif
