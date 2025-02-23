/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebWidget_h
#define WebWidget_h

#include "../platform/WebCanvas.h"
#include "../platform/WebCommon.h"
#include "../platform/WebFloatSize.h"
#include "../platform/WebFrameTimingEvent.h"
#include "../platform/WebInputEventResult.h"
#include "../platform/WebPoint.h"
#include "../platform/WebRect.h"
#include "../platform/WebSize.h"
#include "../platform/WebTopControlsState.h"
#include "WebCompositionUnderline.h"
#include "WebTextDirection.h"
#include "WebTextInputInfo.h"

namespace blink {

class WebCompositeAndReadbackAsyncCallback;
class WebInputEvent;
class WebLayoutAndPaintAsyncCallback;
class WebPagePopup;
class WebString;
struct WebPoint;
template <typename T> class WebVector;

class WebWidget {
public:
    // This method closes and deletes the WebWidget.
    virtual void close() { }

    // Returns the current size of the WebWidget.
    virtual WebSize size() { return WebSize(); }

    // Called to resize the WebWidget.
    virtual void resize(const WebSize&) { }

    // Resizes the unscaled visual viewport. Normally the unscaled visual
    // viewport is the same size as the main frame. The passed size becomes the
    // size of the viewport when unscaled (i.e. scale = 1). This is used to
    // shrink the visible viewport to allow things like the ChromeOS virtual
    // keyboard to overlay over content but allow scrolling it into view.
    virtual void resizeVisualViewport(const WebSize&) { }

    // Called to notify the WebWidget of entering/exiting fullscreen mode.
    virtual void didEnterFullScreen() { }
    virtual void didExitFullScreen() { }

    // Called to update imperative animation state. This should be called before
    // paint, although the client can rate-limit these calls.
    virtual void beginFrame(double lastFrameTimeMonotonic) { }

    // Called to run through the entire set of document lifecycle phases needed
    // to render a frame of the web widget. This MUST be called before Paint,
    // and it may result in calls to WebWidgetClient::didInvalidateRect.
    virtual void updateAllLifecyclePhases() { }

    // Called to paint the rectangular region within the WebWidget
    // onto the specified canvas at (viewPort.x,viewPort.y). You MUST call
    // Layout before calling this method. It is okay to call paint
    // multiple times once layout has been called, assuming no other
    // changes are made to the WebWidget (e.g., once events are
    // processed, it should be assumed that another call to layout is
    // warranted before painting again).
    virtual void paint(WebCanvas*, const WebRect& viewPort) { }

    virtual void paintCompositedDeprecated(WebCanvas*, const WebRect&) { }

    // Run layout and paint of all pending document changes asynchronously.
    // The caller is resposible for keeping the WebLayoutAndPaintAsyncCallback
    // object alive until it is called.
    virtual void layoutAndPaintAsync(WebLayoutAndPaintAsyncCallback*) { }

    // The caller is responsible for keeping the WebCompositeAndReadbackAsyncCallback
    // object alive until it is called. This should only be called when
    // isAcceleratedCompositingActive() is true.
    virtual void compositeAndReadbackAsync(WebCompositeAndReadbackAsyncCallback*) { }

    // Called to inform the WebWidget of a change in theme.
    // Implementors that cache rendered copies of widgets need to re-render
    // on receiving this message
    virtual void themeChanged() { }

    // Called to inform the WebWidget of an input event.
    virtual WebInputEventResult handleInputEvent(const WebInputEvent&) { return WebInputEventResult::NotHandled; }

    // Called to inform the WebWidget of the mouse cursor's visibility.
    virtual void setCursorVisibilityState(bool isVisible) { }

    // Check whether the given point hits any registered touch event handlers.
    virtual bool hasTouchEventHandlersAt(const WebPoint&) { return true; }

    // Applies viewport related properties during a commit from the compositor
    // thread.
    virtual void applyViewportDeltas(
        const WebFloatSize& visualViewportDelta,
        const WebFloatSize& layoutViewportDelta,
        const WebFloatSize& elasticOverscrollDelta,
        float scaleFactor,
        float topControlsShownRatioDelta) { }

    // Records composite or render events for the Performance Timeline.
    // See http://w3c.github.io/frame-timing/ for definition of terms.
    enum FrameTimingEventType {
        CompositeEvent,
        RenderEvent,
    };
    virtual void recordFrameTimingEvent(FrameTimingEventType eventType, int64_t RectId, const WebVector<WebFrameTimingEvent>& events) { }

    // Called to inform the WebWidget that mouse capture was lost.
    virtual void mouseCaptureLost() { }

    // Called to inform the WebWidget that it has gained or lost keyboard focus.
    virtual void setFocus(bool) { }

    // Called to inform the WebWidget of a new composition text.
    // If selectionStart and selectionEnd has the same value, then it indicates
    // the input caret position. If the text is empty, then the existing
    // composition text will be cancelled.
    // Returns true if the composition text was set successfully.
    virtual bool setComposition(
        const WebString& text,
        const WebVector<WebCompositionUnderline>& underlines,
        int selectionStart,
        int selectionEnd) { return false; }

    enum ConfirmCompositionBehavior {
        DoNotKeepSelection,
        KeepSelection,
    };

    // Called to inform the WebWidget to confirm an ongoing composition.
    // This method is same as confirmComposition(WebString());
    // Returns true if there is an ongoing composition.
    virtual bool confirmComposition() { return false; } // Deprecated
    virtual bool confirmComposition(ConfirmCompositionBehavior selectionBehavior) { return false; }

    // Called to inform the WebWidget to confirm an ongoing composition with a
    // new composition text. If the text is empty then the current composition
    // text is confirmed. If there is no ongoing composition, then deletes the
    // current selection and inserts the text. This method has no effect if
    // there is no ongoing composition and the text is empty.
    // Returns true if there is an ongoing composition or the text is inserted.
    virtual bool confirmComposition(const WebString& text) { return false; }

    // Fetches the character range of the current composition, also called the
    // "marked range." Returns true and fills the out-paramters on success;
    // returns false on failure.
    virtual bool compositionRange(size_t* location, size_t* length) { return false; }

    // Returns information about the current text input of this WebWidget.
    // Note that this query can be expensive for long fields, as it returns the
    // plain-text representation of the current editable element. Consider using
    // the lighter-weight textInputType() when appropriate.
    virtual WebTextInputInfo textInputInfo() { return WebTextInputInfo(); }

    // Returns the type of current text input of this WebWidget.
    virtual WebTextInputType textInputType() { return WebTextInputTypeNone; }

    // Returns the anchor and focus bounds of the current selection.
    // If the selection range is empty, it returns the caret bounds.
    virtual bool selectionBounds(WebRect& anchor, WebRect& focus) const { return false; }

    // Returns the text direction at the start and end bounds of the current selection.
    // If the selection range is empty, it returns false.
    virtual bool selectionTextDirection(WebTextDirection& start, WebTextDirection& end) const { return false; }

    // Returns true if the selection range is nonempty and its anchor is first
    // (i.e its anchor is its start).
    virtual bool isSelectionAnchorFirst() const { return false; }

    // Fetch the current selection range of this WebWidget. If there is no
    // selection, it will output a 0-length range with the location at the
    // caret. Returns true and fills the out-paramters on success; returns false
    // on failure.
    virtual bool caretOrSelectionRange(size_t* location, size_t* length) { return false; }

    // Changes the text direction of the selected input node.
    virtual void setTextDirection(WebTextDirection) { }

    // Returns true if the WebWidget uses GPU accelerated compositing
    // to render its contents.
    virtual bool isAcceleratedCompositingActive() const { return false; }

    // Returns true if the WebWidget created is of type WebView.
    virtual bool isWebView() const { return false; }
    // Returns true if the WebWidget created is of type WebPagePopup.
    virtual bool isPagePopup() const { return false; }

    // The WebLayerTreeView initialized on this WebWidgetClient will be going away and
    // is no longer safe to access.
    virtual void willCloseLayerTreeView() { }

    // Calling WebWidgetClient::requestPointerLock() will result in one
    // return call to didAcquirePointerLock() or didNotAcquirePointerLock().
    virtual void didAcquirePointerLock() { }
    virtual void didNotAcquirePointerLock() { }

    // Pointer lock was held, but has been lost. This may be due to a
    // request via WebWidgetClient::requestPointerUnlock(), or for other
    // reasons such as the user exiting lock, window focus changing, etc.
    virtual void didLosePointerLock() { }

    // Informs the WebWidget that the resizer rect changed. Happens for example
    // on mac, when a widget appears below the WebWidget without changing the
    // WebWidget's size (WebWidget::resize() automatically checks the resizer
    // rect.)
    virtual void didChangeWindowResizerRect() { }

    // The page background color. Can be used for filling in areas without
    // content.
    virtual WebColor backgroundColor() const { return 0xFFFFFFFF; /* SK_ColorWHITE */ }

    // The currently open page popup, which are calendar and datalist pickers
    // but not the select popup.
    virtual WebPagePopup* pagePopup() const { return 0; }

    // Notification about the top controls height.  If the boolean is true, then
    // the embedder shrunk the WebView size by the top controls height.
    virtual void setTopControlsHeight(float height, bool topControlsShrinkLayoutSize) { }

    // Updates top controls constraints and current state. Allows embedder to
    // control what are valid states for top controls and if it should animate.
    virtual void updateTopControlsState(WebTopControlsState constraints, WebTopControlsState current, bool animate) { }

protected:
    ~WebWidget() { }
};

} // namespace blink

#endif
