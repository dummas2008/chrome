// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/dom/RemoteSecurityContext.h"

#include "core/frame/csp/ContentSecurityPolicy.h"
#include "platform/weborigin/SecurityOrigin.h"

namespace blink {

RemoteSecurityContext::RemoteSecurityContext()
    : SecurityContext()
{
    // RemoteSecurityContext's origin is expected to stay uninitialized until
    // we set it using replicated origin data from the browser process.
    DCHECK(!getSecurityOrigin());

    // CSP will not be replicated for RemoteSecurityContexts, as it is moving
    // to the browser process.  For now, initialize CSP to a default
    // locked-down policy.
    setContentSecurityPolicy(ContentSecurityPolicy::create());

    // FIXME: Document::initSecurityContext has a few other things we may
    // eventually want here, such as enforcing a setting to
    // grantUniversalAccess().
}

RemoteSecurityContext* RemoteSecurityContext::create()
{
    return new RemoteSecurityContext();
}

DEFINE_TRACE(RemoteSecurityContext)
{
    SecurityContext::trace(visitor);
}

void RemoteSecurityContext::setReplicatedOrigin(PassRefPtr<SecurityOrigin> origin)
{
    setSecurityOrigin(origin);
}


} // namespace blink
