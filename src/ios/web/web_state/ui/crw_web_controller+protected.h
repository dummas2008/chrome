// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_WEB_STATE_UI_CRW_WEB_CONTROLLER_PROTECTED_H_
#define IOS_WEB_WEB_STATE_UI_CRW_WEB_CONTROLLER_PROTECTED_H_

#import "ios/web/web_state/ui/crw_web_controller.h"

#import <WebKit/WebKit.h>

#include <memory>

#import "base/mac/objc_property_releaser.h"
#include "base/mac/scoped_nsobject.h"
#import "ios/web/public/navigation_manager.h"
#include "ios/web/public/referrer.h"
#include "ios/web/public/web_state/page_display_state.h"
#import "ios/web/web_state/crw_pass_kit_downloader.h"
#import "ios/web/web_state/ui/wk_back_forward_list_item_holder.h"

@class CRWSessionController;
namespace web {
struct FrameInfo;
class NavigationItem;
}  // namespace web

namespace web {
// Values of the UMA |Web.URLVerificationFailure| histogram.
enum WebViewDocumentType {
  // Generic contents (e.g. PDF documents).
  WEB_VIEW_DOCUMENT_TYPE_GENERIC = 0,
  // HTML contents.
  WEB_VIEW_DOCUMENT_TYPE_HTML,
  // Unknown contents.
  WEB_VIEW_DOCUMENT_TYPE_UNKNOWN,
  WEB_VIEW_DOCUMENT_TYPE_COUNT,
};
}  // namespace web

using web::NavigationManager;
namespace {
// Constants for storing the source of NSErrors received by WKWebViews:
// - Errors received by |-webView:didFailProvisionalNavigation:withError:| are
//   recorded using WKWebViewErrorSource::PROVISIONAL_LOAD.  These should be
//   cancelled.
// - Errors received by |-webView:didFailNavigation:withError:| are recorded
//   using WKWebViewsource::NAVIGATION.  These errors should not be cancelled,
//   as the WKWebView will automatically retry the load.
static NSString* const kWKWebViewErrorSourceKey = @"ErrorSource";
typedef enum { NONE = 0, PROVISIONAL_LOAD, NAVIGATION } WKWebViewErrorSource;
}  // namespace

// URL scheme for messages sent from javascript for asynchronous processing.
static NSString* const kScriptMessageName = @"crwebinvoke";
// URL scheme for messages sent from javascript for immediate processing.
static NSString* const kScriptImmediateName = @"crwebinvokeimmediate";

#pragma mark -

// A container object for any navigation information that is only available
// during pre-commit delegate callbacks, and thus must be held until the
// navigation commits and the informatino can be used.
@interface CRWWebControllerPendingNavigationInfo : NSObject {
  base::mac::ObjCPropertyReleaser
      _propertyReleaser_CRWWebControllerPendingNavigationInfo;
}
// The referrer for the page.
@property(nonatomic, copy) NSString* referrer;
// The MIME type for the page.
@property(nonatomic, copy) NSString* MIMEType;
// The navigation type for the load.
@property(nonatomic, assign) WKNavigationType navigationType;
// Whether the pending navigation has been directly cancelled before the
// navigation is committed.
// Cancelled navigations should be simply discarded without handling any
// specific error.
@property(nonatomic, assign) BOOL cancelled;
@end

#pragma mark -

// Category for methods used or implemented by implementation subclasses of
// CRWWebController.
@interface CRWWebController (ProtectedMethods)

#pragma mark Methods implemented by subclasses
// Everything in this section must be implemented by subclasses.

// If |contentView_| contains a web view, this is the web view it contains.
// If not, it's nil.
@property(nonatomic, readonly) WKWebView* webView;

// The scroll view of |webView|.
@property(nonatomic, readonly) UIScrollView* webScrollView;

// The title of the page.
@property(nonatomic, readonly) NSString* title;

// A set of script managers whose scripts have been injected into the current
// page.
@property(nonatomic, readonly) NSMutableSet* injectedScriptManagers;

// Downloader for PassKit files. Lazy initialized.
@property(nonatomic, readonly) CRWPassKitDownloader* passKitDownloader;

- (CRWWebControllerPendingNavigationInfo*)pendingNavigationInfo;

// Designated initializer.
- (instancetype)initWithWebState:(std::unique_ptr<web::WebStateImpl>)webState;

// Creates a web view if it's not yet created.
- (void)ensureWebViewCreated;

// Destroys the web view by setting webView property to nil.
- (void)resetWebView;

// Returns the current URL of the web view, and sets |trustLevel| accordingly
// based on the confidence in the verification.
- (GURL)webURLWithTrustLevel:(web::URLVerificationTrustLevel*)trustLevel;

// Returns the type of document object loaded in the web view.
- (web::WebViewDocumentType)webViewDocumentType;

//- (void)loadRequest:(NSMutableURLRequest*)request;

// Called before loading current URL in WebView.
- (void)willLoadCurrentURLInWebView;

// Loads request for the URL of the current navigation item. Subclasses may
// choose to build a new NSURLRequest and call |loadRequest| on the underlying
// web view, or use native web view navigation where possible (for example,
// going back and forward through the history stack).
- (void)loadRequestForCurrentNavigationItem;

// Cancels any load in progress in the web view.
- (void)abortWebLoad;

// Sets zoom scale value for webview scroll view from |zoomState|.
- (void)applyWebViewScrollZoomScaleFromZoomState:
    (const web::PageZoomState&)zoomState;

// Creates a web view with given |config|. No-op if web view is already created.
- (void)ensureWebViewCreatedWithConfiguration:(WKWebViewConfiguration*)config;

// Called when web controller receives a new message from the web page.
- (void)didReceiveScriptMessage:(WKScriptMessage*)message;

#pragma mark - Optional methods for subclasses
// Subclasses may overwrite methods in this section.

// Checks if the URL has changed unexpectedly, and handles such changes.
// Returns true if the URL has changed.
// TODO(stuartmorgan): Remove once the hook points are driven from the subclass.
- (BOOL)checkForUnexpectedURLChange;

// Handles 'window.history.willChangeState' message.
- (BOOL)handleWindowHistoryWillChangeStateMessage:
            (base::DictionaryValue*)message
                                          context:(NSDictionary*)context;
// Handles 'window.history.didPushState' message.
- (BOOL)handleWindowHistoryDidPushStateMessage:(base::DictionaryValue*)message
                                       context:(NSDictionary*)context;

// Handles 'window.history.didReplaceState' message.
- (BOOL)handleWindowHistoryDidReplaceStateMessage:
    (base::DictionaryValue*)message
                                          context:(NSDictionary*)context;

// Sets up WebUI for URL.
- (void)createWebUIForURL:(const GURL&)URL;

// Clears WebUI, if one exists.
- (void)clearWebUI;

// Returns a NSMutableURLRequest that represents the current NavigationItem.
- (NSMutableURLRequest*)requestForCurrentNavigationItem;

// Compares the two URLs being navigated between during a history navigation to
// determine if a # needs to be appended to the URL of |toItem| to trigger a
// hashchange event. If so, also saves the modified URL into |toItem|.
- (GURL)URLForHistoryNavigationFromItem:(web::NavigationItem*)fromItem
                                 toItem:(web::NavigationItem*)toItem;

// Updates the internal state and informs the delegate that any outstanding load
// operations are cancelled.
- (void)loadCancelled;

// Called when a load ends in an error.
// TODO(stuartmorgan): Figure out if there's actually enough shared logic that
// this makes sense. At the very least remove inMainFrame since that only makes
// sense for UIWebView.
- (void)handleLoadError:(NSError*)error inMainFrame:(BOOL)inMainFrame;

#pragma mark - Internal methods for use by subclasses

// The web view's view of the current URL. During page transitions
// this may not be the same as the session history's view of the current URL.
// This method can change the state of the CRWWebController, as it will display
// an error if the returned URL is not reliable from a security point of view.
// Note that this method is expensive, so it should always be cached locally if
// it's needed multiple times in a method.
@property(nonatomic, readonly) GURL currentURL;

// The default URL for a newly created web view.
@property(nonatomic, readonly) const GURL& defaultURL;

// Last URL change reported to webDidStartLoadingURL. Used to detect page
// location changes in practice.
@property(nonatomic, readonly) GURL URLOnStartLoading;

// Last URL change registered for load request.
@property(nonatomic, readonly) GURL lastRegisteredRequestURL;

// Returns YES if the object is being deallocated.
@property(nonatomic, readonly) BOOL isBeingDestroyed;

// Return YES if network activity is being halted. Halting happens prior to
// destruction.
@property(nonatomic, readonly) BOOL isHalted;

// Returns whether the user is interacting with the page.
@property(nonatomic, readonly) BOOL userIsInteracting;

// YES if a user interaction has been registered at any time since the page has
// loaded.
@property(nonatomic, readwrite) BOOL userInteractionRegistered;

// Whether the web page is currently performing window.history.pushState or
// window.history.replaceState
@property(nonatomic, readonly) BOOL changingHistoryState;

// Returns the current window id.
@property(nonatomic, readonly) NSString* windowId;

// Returns windowID that is saved when a page changes. Used to detect refreshes.
@property(nonatomic, readonly) NSString* lastSeenWindowID;

// Returns NavigationManager's session controller.
@property(nonatomic, readonly) CRWSessionController* sessionController;

// Returns a new script which wraps |script| with windowID check so |script| is
// not evaluated on windowID mismatch.
- (NSString*)scriptByAddingWindowIDCheckForScript:(NSString*)script;

// Removes webView, optionally tracking the URL of the evicted
// page for later cache-based reconstruction.
- (void)removeWebViewAllowingCachedReconstruction:(BOOL)allowCache;

// Subclasses must call this method every time when web view has been created
// or recreated. This method should not be called if a web view property has
// changed (e.g. view's background color). Web controller adds |webView| to its
// content view.
- (void)webViewDidChange;

// Aborts any load for both the web view and web controller.
- (void)abortLoad;

// Returns the URL that the navigation system believes should be currently
// active.
// TODO(stuartmorgan):Remove this in favor of more specific getters.
- (const GURL&)currentNavigationURL;

// Returns the WKBackForwardListItemHolder for the current navigation item.
- (web::WKBackForwardListItemHolder*)currentBackForwardListItemHolder;

// Updates the WKBackForwardListItemHolder navigation item.
- (void)updateCurrentBackForwardListItemHolder;

// Extracts navigation info from WKNavigationAction and sets it as a pending.
// Some pieces of navigation information are only known in
// |decidePolicyForNavigationAction|, but must be in a pending state until
// |didgo/Navigation| where it becames current.
- (void)updatePendingNavigationInfoFromNavigationAction:
    (WKNavigationAction*)action;

// Extracts navigation info from WKNavigationResponse and sets it as a pending.
// Some pieces of navigation information are only known in
// |decidePolicyForNavigationResponse|, but must be in a pending state until
// |didCommitNavigation| where it becames current.
- (void)updatePendingNavigationInfoFromNavigationResponse:
    (WKNavigationResponse*)response;

// Updates current state with any pending information. Should be called when a
// navigation is committed.
- (void)commitPendingNavigationInfo;

// Called when the web page has changed document and/or URL, and so the page
// navigation should be reported to the delegate, and internal state updated to
// reflect the fact that the navigation has occurred.
// TODO(stuartmorgan): The code conflates URL changes and document object
// changes; the two need to be separated and handled differently.
- (void)webPageChanged;

// Injects all scripts registered for early injection, as well as the window ID,
// if necssary. If they are already injected, this is a no-op.
- (void)injectEarlyInjectionScripts;

// Inject windowID if not yet injected.
- (void)injectWindowID;

// Called when a page (native or web) has actually started loading (i.e., for
// a web page the document has actually changed), or after the load request has
// been registered for a non-document-changing URL change. Updates internal
// state not specific to web pages, and informs the delegate.
- (void)didStartLoadingURL:(const GURL&)URL updateHistory:(BOOL)updateHistory;

// Returns YES if the user interacted with the page recently.
- (BOOL)userClickedRecently;

// Returns whether the desktop user agent should be used when setting the user
// agent.
- (BOOL)useDesktopUserAgent;

// Called when SSL status has been updated for the current navigation item.
- (void)didUpdateSSLStatusForCurrentNavigationItem;

// Processes the given web invocation; urlSchemeIsWebInvoke: must return YES
// for the given URL. |request| should be the request associated with that load.
- (void)handleWebInvokeURL:(const GURL&)url request:(NSURLRequest*)request;

// Returns YES if the given load request should be allowed to continue. If this
// returns NO, the load should be cancelled. |targetFrame| contains information
// about the frame to which navigation is targeted, can be null.
// |isLinkClick| should indicate whether the navigation is the
// result of a link click (either directly, or via JS triggered by a link).
- (BOOL)shouldAllowLoadWithRequest:(NSURLRequest*)request
                       targetFrame:(const web::FrameInfo*)targetFrame
                       isLinkClick:(BOOL)isLinkClick;

// Prepares web controller and delegates for anticipated page change.
// Allows several methods to invoke webWill/DidAddPendingURL on anticipated page
// change, using the same cached request and calculated transition types.
- (void)registerLoadRequest:(const GURL&)URL
                   referrer:(const web::Referrer&)referrer
                 transition:(ui::PageTransition)transition;

// Update the appropriate parts of the model and broadcast to the embedder. This
// may be called multiple times and thus must be idempotent.
- (void)loadCompleteWithSuccess:(BOOL)loadSuccess;

// Adds an activity indicator tasks for this web controller.
- (void)addActivityIndicatorTask;
// Clears all activity indicator tasks for this web controller.
- (void)clearActivityIndicatorTasks;

// Creates a new opened by DOM window and returns its autoreleased web
// controller.
- (CRWWebController*)createChildWebController;

// Called following navigation completion to generate final navigation lifecycle
// events. Navigation is considered complete when the document has finished
// loading, or when other page load mechanics are completed on a
// non-document-changing URL change.
- (void)didFinishNavigation;

// Called when a JavaScript dialog, HTTP authentication dialog or window.open
// call has been suppressed.
- (void)didSuppressDialog;

// Returns the referrer policy for the given referrer policy string (as reported
// from JS).
- (web::ReferrerPolicy)referrerPolicyFromString:(const std::string&)policy;

// Returns YES if the popup should be blocked, NO otherwise.
- (BOOL)shouldBlockPopupWithURL:(const GURL&)popupURL
                      sourceURL:(const GURL&)sourceURL;

// Call to stop web controller activity, in particular to stop all network
// requests. Called as part of the close sequence if it hasn't already been
// halted; should also be called from the web delegate as part of any shutdown
// sequence which doesn't call -close.
- (void)terminateNetworkActivity;

// Acts on a single message from the JS object, parsed from JSON into a
// DictionaryValue. Returns NO if the format for the message was invalid.
- (BOOL)respondToMessage:(base::DictionaryValue*)crwMessage
       userIsInteracting:(BOOL)userIsInteracting
               originURL:(const GURL&)originURL;

// Asynchronously determines window size of the web page. |handler| cannot
// be nil.
- (void)fetchWebPageSizeWithCompletionHandler:(void (^)(CGSize))handler;

// Returns the current entry from the underlying session controller.
// TODO(stuartmorgan): Audit all calls to these methods; these are just wrappers
// around the same logic as GetActiveEntry, so should probably not be used for
// the same reason that GetActiveEntry is deprecated. (E.g., page operations
// should generally be dealing with the last commited entry, not a pending
// entry).
- (CRWSessionEntry*)currentSessionEntry;
// Returns the navigation item for the current page.
- (web::NavigationItem*)currentNavItem;

// The HTTP headers associated with the current navigation item. These are nil
// unless the request was a POST.
- (NSDictionary*)currentHTTPHeaders;

// Returns the referrer for the current page.
- (web::Referrer)currentReferrer;

// Returns the referrer for current navigation item. May be empty.
- (web::Referrer)currentSessionEntryReferrer;

// Returns the current transition type.
- (ui::PageTransition)currentTransition;

// Returns the current entry from the underlying session controller.
- (CRWSessionEntry*)currentSessionEntry;

// Resets pending external request information.
- (void)resetExternalRequest;

// Resets pending navigation info.
- (void)resetPendingNavigationInfo;

// Converts MIME type string to WebViewDocumentType.
- (web::WebViewDocumentType)documentTypeFromMIMEType:(NSString*)MIMEType;

// Extracts Referer value from WKNavigationAction request header.
- (NSString*)refererFromNavigationAction:(WKNavigationAction*)action;

@end

#endif  // IOS_WEB_WEB_STATE_UI_CRW_WEB_CONTROLLER_PROTECTED_H_
