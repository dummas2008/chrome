// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_URL_REQUEST_URL_REQUEST_FILTER_H_
#define NET_URL_REQUEST_URL_REQUEST_FILTER_H_

#include <map>
#include <string>
#include <unordered_map>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "net/base/net_export.h"
#include "net/url_request/url_request_interceptor.h"

class GURL;

namespace net {
class URLRequest;
class URLRequestJob;
class URLRequestInterceptor;

// A class to help filter URLRequest jobs based on the URL of the request
// rather than just the scheme.  Example usage:
//
// // Intercept "scheme://host/" requests.
// URLRequestFilter::GetInstance()->AddHostnameInterceptor(
//     "scheme", "host", std::move(interceptor));
// // Add special handling for the URL http://foo.com/
// URLRequestFilter::GetInstance()->AddUrlInterceptor(
//     GURL("http://foo.com/"), std::move(interceptor));
//
// The URLRequestFilter is implemented as a singleton that is not thread-safe,
// and hence must only be used in test code where the network stack is used
// from a single thread. It must only be accessed on that networking thread.
// One exception is that during startup, before any message loops have been
// created, interceptors may be added (the session restore tests rely on this).
// If the URLRequestFilter::MaybeInterceptRequest can't find a handler for a
// request, it returns NULL and lets the configured ProtocolHandler handle the
// request.
class NET_EXPORT URLRequestFilter : public URLRequestInterceptor {
 public:
  // Singleton instance for use.
  static URLRequestFilter* GetInstance();

  void AddHostnameInterceptor(
      const std::string& scheme,
      const std::string& hostname,
      scoped_ptr<URLRequestInterceptor> interceptor);
  void RemoveHostnameHandler(const std::string& scheme,
                             const std::string& hostname);

  // Returns true if we successfully added the URL handler.  This will replace
  // old handlers for the URL if one existed.
  bool AddUrlInterceptor(const GURL& url,
                         scoped_ptr<URLRequestInterceptor> interceptor);

  void RemoveUrlHandler(const GURL& url);

  // Clear all the existing URL handlers and unregister with the
  // ProtocolFactory.  Resets the hit count.
  void ClearHandlers();

  // Returns the number of times a handler was used to service a request.
  int hit_count() const { return hit_count_; }

  // URLRequestInterceptor implementation:
  URLRequestJob* MaybeInterceptRequest(
      URLRequest* request,
      NetworkDelegate* network_delegate) const override;

 private:
  // scheme,hostname -> URLRequestInterceptor
  using HostnameInterceptorMap = std::map<std::pair<std::string, std::string>,
                                          scoped_ptr<URLRequestInterceptor>>;
  // URL -> URLRequestInterceptor
  using URLInterceptorMap =
      std::unordered_map<std::string, scoped_ptr<URLRequestInterceptor>>;

  URLRequestFilter();
  ~URLRequestFilter() override;

  // Maps hostnames to interceptors.  Hostnames take priority over URLs.
  HostnameInterceptorMap hostname_interceptor_map_;

  // Maps URLs to interceptors.
  URLInterceptorMap url_interceptor_map_;

  mutable int hit_count_;

  // Singleton instance.
  static URLRequestFilter* shared_instance_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestFilter);
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_FILTER_H_
