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

#ifndef WebInputEvent_h
#define WebInputEvent_h

#include "../platform/WebCommon.h"
#include "../platform/WebGestureDevice.h"
#include "../platform/WebPointerProperties.h"
#include "../platform/WebRect.h"
#include "WebTouchPoint.h"

#include <string.h>

namespace blink {

// The classes defined in this file are intended to be used with
// WebWidget's handleInputEvent method.  These event types are cross-
// platform and correspond closely to WebCore's Platform*Event classes.
//
// WARNING! These classes must remain PODs (plain old data).  They are
// intended to be "serializable" by copying their raw bytes, so they must
// not contain any non-bit-copyable member variables!
//
// Furthermore, the class members need to be packed so they are aligned
// properly and don't have paddings/gaps, otherwise memory check tools
// like Valgrind will complain about uninitialized memory usage when
// transferring these classes over the wire.

#pragma pack(push, 4)

// WebInputEvent --------------------------------------------------------------

class WebInputEvent {
public:
    // When we use an input method (or an input method editor), we receive
    // two events for a keypress. The former event is a keydown, which
    // provides a keycode, and the latter is a textinput, which provides
    // a character processed by an input method. (The mapping from a
    // keycode to a character code is not trivial for non-English
    // keyboards.)
    // To support input methods, Safari sends keydown events to WebKit for
    // filtering. WebKit sends filtered keydown events back to Safari,
    // which sends them to input methods.
    // Unfortunately, it is hard to apply this design to Chrome because of
    // our multiprocess architecture. An input method is running in a
    // browser process. On the other hand, WebKit is running in a renderer
    // process. So, this design results in increasing IPC messages.
    // To support input methods without increasing IPC messages, Chrome
    // handles keyboard events in a browser process and send asynchronous
    // input events (to be translated to DOM events) to a renderer
    // process.
    // This design is mostly the same as the one of Windows and Mac Carbon.
    // So, for what it's worth, our Linux and Mac front-ends emulate our
    // Windows front-end. To emulate our Windows front-end, we can share
    // our back-end code among Windows, Linux, and Mac.
    // TODO(hbono): Issue 18064: remove the KeyDown type since it isn't
    // used in Chrome any longer.

    // A Java counterpart will be generated for this enum.
    // GENERATED_JAVA_ENUM_PACKAGE: org.chromium.blink_public.web
    // GENERATED_JAVA_CLASS_NAME_OVERRIDE: WebInputEventType
    enum Type {
        Undefined = -1,
        TypeFirst = Undefined,

        // WebMouseEvent
        MouseDown,
        MouseTypeFirst = MouseDown,
        MouseUp,
        MouseMove,
        MouseEnter,
        MouseLeave,
        ContextMenu,
        MouseTypeLast = ContextMenu,

        // WebMouseWheelEvent
        MouseWheel,

        // WebKeyboardEvent
        RawKeyDown,
        KeyboardTypeFirst = RawKeyDown,
        KeyDown,
        KeyUp,
        Char,
        KeyboardTypeLast = Char,

        // WebGestureEvent
        GestureScrollBegin,
        GestureTypeFirst = GestureScrollBegin,
        GestureScrollEnd,
        GestureScrollUpdate,
        GestureFlingStart,
        GestureFlingCancel,
        GestureShowPress,
        GestureTap,
        GestureTapUnconfirmed,
        GestureTapDown,
        GestureTapCancel,
        GestureDoubleTap,
        GestureTwoFingerTap,
        GestureLongPress,
        GestureLongTap,
        GesturePinchBegin,
        GesturePinchEnd,
        GesturePinchUpdate,
        GestureTypeLast = GesturePinchUpdate,

        // WebTouchEvent
        TouchStart,
        TouchTypeFirst = TouchStart,
        TouchMove,
        TouchEnd,
        TouchCancel,
        TouchTypeLast = TouchCancel,

        TypeLast = TouchTypeLast
    };

    // The modifier constants cannot change their values since pepper
    // does a 1-1 mapping of its values; see
    // content/renderer/pepper/event_conversion.cc
    //
    // A Java counterpart will be generated for this enum.
    // GENERATED_JAVA_ENUM_PACKAGE: org.chromium.blink_public.web
    // GENERATED_JAVA_CLASS_NAME_OVERRIDE: WebInputEventModifier
    enum Modifiers {
        // modifiers for all events:
        ShiftKey         = 1 << 0,
        ControlKey       = 1 << 1,
        AltKey           = 1 << 2,
        MetaKey          = 1 << 3,

        // modifiers for keyboard events:
        IsKeyPad         = 1 << 4,
        IsAutoRepeat     = 1 << 5,

        // modifiers for mouse events:
        LeftButtonDown   = 1 << 6,
        MiddleButtonDown = 1 << 7,
        RightButtonDown  = 1 << 8,

        // Toggle modifers for all events.
        CapsLockOn       = 1 << 9,
        NumLockOn        = 1 << 10,

        IsLeft           = 1 << 11,
        IsRight          = 1 << 12,

        // Indicates that an event was generated on the touch screen while
        // touch accessibility is enabled, so the event should be handled
        // by accessibility code first before normal input event processing.
        IsTouchAccessibility = 1 << 13,

        IsComposing      = 1 << 14,

        AltGrKey         = 1 << 15,
        OSKey            = 1 << 16,
        FnKey            = 1 << 17,
        SymbolKey        = 1 << 18,

        ScrollLockOn     = 1 << 19,
    };

    // The rail mode for a wheel event specifies the axis on which scrolling is
    // expected to stick. If this axis is set to Free, then scrolling is not
    // stuck to any axis.
    enum RailsMode {
        RailsModeFree       = 0,
        RailsModeHorizontal = 1,
        RailsModeVertical   = 2,
    };

    static const int InputModifiers = ShiftKey | ControlKey | AltKey | MetaKey;

    double timeStampSeconds; // Seconds since platform start with microsecond resolution.
    unsigned size; // The size of this structure, for serialization.
    Type type;
    int modifiers;

    // Returns true if the WebInputEvent |type| is a mouse event.
    static bool isMouseEventType(int type)
    {
        return MouseTypeFirst <= type && type <= MouseTypeLast;
    }

    // Returns true if the WebInputEvent |type| is a keyboard event.
    static bool isKeyboardEventType(int type)
    {
        return KeyboardTypeFirst <= type && type <= KeyboardTypeLast;
    }

    // Returns true if the WebInputEvent |type| is a touch event.
    static bool isTouchEventType(int type)
    {
        return TouchTypeFirst <= type && type <= TouchTypeLast;
    }

    // Returns true if the WebInputEvent is a gesture event.
    static bool isGestureEventType(int type)
    {
        return GestureTypeFirst <= type && type <= GestureTypeLast;
    }

protected:
    explicit WebInputEvent(unsigned sizeParam)
    {
        memset(this, 0, sizeParam);
        timeStampSeconds = 0.0;
        size = sizeParam;
        type = Undefined;
        modifiers = 0;
    }
};

// WebKeyboardEvent -----------------------------------------------------------

class WebKeyboardEvent : public WebInputEvent {
public:
    // Caps on string lengths so we can make them static arrays and keep
    // them PODs.
    static const size_t textLengthCap = 4;

    // http://www.w3.org/TR/DOM-Level-3-Events/keyset.html lists the
    // identifiers.  The longest is 18 characters, so we round up to the
    // next multiple of 4.
    static const size_t keyIdentifierLengthCap = 20;

    // |windowsKeyCode| is the Windows key code associated with this key
    // event.  Sometimes it's direct from the event (i.e. on Windows),
    // sometimes it's via a mapping function.  If you want a list, see
    // WebCore/platform/chromium/KeyboardCodes* . Note that this should
    // ALWAYS store the non-locational version of a keycode as this is
    // what is returned by the Windows API. For example, it should
    // store VK_SHIFT instead of VK_RSHIFT. The location information
    // should be stored in |modifiers|.
    int windowsKeyCode;

    // The actual key code genenerated by the platform.  The DOM spec runs
    // on Windows-equivalent codes (thus |windowsKeyCode| above) but it
    // doesn't hurt to have this one around.
    int nativeKeyCode;

    // The DOM code enum of the key pressed as passed by the embedder. DOM
    // code enum are defined in ui/events/keycodes/dom4/keycode_converter_data.h.
    int domCode;

    // The DOM key enum of the key pressed as passed by the embedder. DOM
    // key enum are defined in ui/events/keycodes/dom3/dom_key_data.h
    int domKey;

    // This identifies whether this event was tagged by the system as being
    // a "system key" event (see
    // http://msdn.microsoft.com/en-us/library/ms646286(VS.85).aspx for
    // details). Other platforms don't have this concept, but it's just
    // easier to leave it always false than ifdef.
    bool isSystemKey;

    // Whether the event forms part of a browser-handled keyboard shortcut.
    // This can be used to conditionally suppress Char events after a
    // shortcut-triggering RawKeyDown goes unhandled.
    bool isBrowserShortcut;

    // |text| is the text generated by this keystroke.  |unmodifiedText| is
    // |text|, but unmodified by an concurrently-held modifiers (except
    // shift).  This is useful for working out shortcut keys.  Linux and
    // Windows guarantee one character per event.  The Mac does not, but in
    // reality that's all it ever gives.  We're generous, and cap it a bit
    // longer.
    WebUChar text[textLengthCap];
    WebUChar unmodifiedText[textLengthCap];

    // This is a string identifying the key pressed.
    char keyIdentifier[keyIdentifierLengthCap];

    WebKeyboardEvent()
        : WebInputEvent(sizeof(WebKeyboardEvent))
        , windowsKeyCode(0)
        , nativeKeyCode(0)
        , isSystemKey(false)
        , isBrowserShortcut(false)
    {
        memset(&text, 0, sizeof(text));
        memset(&unmodifiedText, 0, sizeof(unmodifiedText));
        memset(&keyIdentifier, 0, sizeof(keyIdentifier));
    }

    // Sets keyIdentifier based on the value of windowsKeyCode.  This is
    // handy for generating synthetic keyboard events.
    BLINK_EXPORT void setKeyIdentifierFromWindowsKeyCode();
};

// WebMouseEvent --------------------------------------------------------------

class WebMouseEvent : public WebInputEvent, public WebPointerProperties {
public:
    // Renderer coordinates. Similar to viewport coordinates but without
    // DevTools emulation transform or overscroll applied. i.e. the coordinates
    // in Chromium's RenderView bounds.
    int x;
    int y;

    // DEPRECATED (crbug.com/507787)
    int windowX;
    int windowY;

    // Screen coordinate
    int globalX;
    int globalY;

    int movementX;
    int movementY;
    int clickCount;

    WebMouseEvent()
        : WebInputEvent(sizeof(WebMouseEvent))
        , WebPointerProperties()
        , x(0)
        , y(0)
        , windowX(0)
        , windowY(0)
        , globalX(0)
        , globalY(0)
        , movementX(0)
        , movementY(0)
        , clickCount(0)
    {
    }

protected:
    explicit WebMouseEvent(unsigned sizeParam)
        : WebInputEvent(sizeParam)
        , WebPointerProperties()
        , x(0)
        , y(0)
        , windowX(0)
        , windowY(0)
        , globalX(0)
        , globalY(0)
        , movementX(0)
        , movementY(0)
        , clickCount(0)
    {
    }
};

// WebMouseWheelEvent ---------------------------------------------------------

class WebMouseWheelEvent : public WebMouseEvent {
public:
    enum Phase {
        PhaseNone        = 0,
        PhaseBegan       = 1 << 0,
        PhaseStationary  = 1 << 1,
        PhaseChanged     = 1 << 2,
        PhaseEnded       = 1 << 3,
        PhaseCancelled   = 1 << 4,
        PhaseMayBegin    = 1 << 5,
    };

    float deltaX;
    float deltaY;
    float wheelTicksX;
    float wheelTicksY;

    float accelerationRatioX;
    float accelerationRatioY;

    // This field exists to allow BrowserPlugin to mark MouseWheel events as
    // 'resent' to handle the case where an event is not consumed when first
    // encountered; it should be handled differently by the plugin when it is
    // sent for thesecond time. No code within Blink touches this, other than to
    // plumb it through event conversions.
    int resendingPluginId;

    Phase phase;
    Phase momentumPhase;

    // Rubberbanding is an OSX visual effect. When a user scrolls the content
    // area with a track pad, and the content area is already at its limit in
    // the direction being scrolled, the entire content area is allowed to
    // scroll slightly off screen, revealing a grey background. When the user
    // lets go, the content area snaps back into place. Blink is responsible
    // for this rubberbanding effect, but the embedder may wish to disable
    // rubber banding in the left or right direction, if the scroll should have
    // an alternate effect. The common case is that a scroll in the left or
    // right directions causes a back or forwards navigation, respectively.
    //
    // These flags prevent rubber banding from starting in a given direction,
    // but have no effect on an ongoing rubber banding. A rubber banding that
    // started in the vertical direction is allowed to continue in the right
    // direction, even if canRubberbandRight is 0.
    bool canRubberbandLeft;
    bool canRubberbandRight;

    bool scrollByPage;
    bool hasPreciseScrollingDeltas;

    // When false, this wheel event should not trigger scrolling (or any other default
    // action) if the event goes unhandled by JavaScript. This is used, for example,
    // when the browser decides the default behavior for Ctrl+Wheel should be to zoom
    // instead of scroll.
    bool canScroll;

    RailsMode railsMode;

    WebMouseWheelEvent()
        : WebMouseEvent(sizeof(WebMouseWheelEvent))
        , deltaX(0.0f)
        , deltaY(0.0f)
        , wheelTicksX(0.0f)
        , wheelTicksY(0.0f)
        , accelerationRatioX(1.0f)
        , accelerationRatioY(1.0f)
        , resendingPluginId(-1)
        , phase(PhaseNone)
        , momentumPhase(PhaseNone)
        , canRubberbandLeft(true)
        , canRubberbandRight(true)
        , scrollByPage(false)
        , hasPreciseScrollingDeltas(false)
        , canScroll(true)
        , railsMode(RailsModeFree)
    {
    }
};

// WebGestureEvent --------------------------------------------------------------

class WebGestureEvent : public WebInputEvent {
public:
    enum ScrollUnits {
        PrecisePixels = 0, // generated by high precision devices.
        Pixels, // large pixel jump duration; should animate to delta.
        Page // page (visible viewport) based scrolling.
    };

    int x;
    int y;
    int globalX;
    int globalY;
    WebGestureDevice sourceDevice;
    // This field exists to allow BrowserPlugin to mark GestureScroll events as
    // 'resent' to handle the case where an event is not consumed when first
    // encountered; it should be handled differently by the plugin when it is
    // sent for thesecond time. No code within Blink touches this, other than to
    // plumb it through event conversions.
    int resendingPluginId;

    union {
        // Tap information must be set for GestureTap, GestureTapUnconfirmed,
        // and GestureDoubleTap events.
        struct {
            int tapCount;
            float width;
            float height;
        } tap;

        struct {
            float width;
            float height;
        } tapDown;

        struct {
            float width;
            float height;
        } showPress;

        struct {
            float width;
            float height;
        } longPress;

        struct {
            float firstFingerWidth;
            float firstFingerHeight;
        } twoFingerTap;

        struct {
            // Initial motion that triggered the scroll.
            // May be redundant with deltaX/deltaY in the first scrollUpdate.
            float deltaXHint;
            float deltaYHint;
            // Default initialized to ScrollUnits::PrecisePixels.
            ScrollUnits deltaHintUnits;
            // If true, this event will skip hit testing to find a scroll
            // target and instead just scroll the viewport.
            bool targetViewport;
            // If true, this event comes after a non-inertial gesture
            // scroll sequence; OSX has unique phases for normal and
            // momentum scroll events. Should always be false for touch based
            // input as it generates GestureFlingStart instead.
            bool inertial;
            // True if this event was synthesized in order to force a hit test; avoiding scroll
            // latching behavior until crbug.com/526463 is fully implemented.
            bool synthetic;
        } scrollBegin;

        struct {
            float deltaX;
            float deltaY;
            float velocityX;
            float velocityY;
            // Whether any previous GestureScrollUpdate in the current scroll
            // sequence was suppressed (e.g., the causal touchmove was
            // preventDefault'ed). This bit is particularly useful for
            // determining whether the observed scroll update sequence captures
            // the entirety of the generative motion.
            bool previousUpdateInSequencePrevented;
            bool preventPropagation;
            bool inertial;
            // Default initialized to ScrollUnits::PrecisePixels.
            ScrollUnits deltaUnits;
        } scrollUpdate;

        struct {
            // The original delta units the scrollBegin and scrollUpdates
            // were sent as.
            ScrollUnits deltaUnits;
            // If true, this event comes after an inertial gesture
            // scroll sequence; OSX has unique phases for normal and
            // momentum scroll events. Should always be false for touch based
            // input as it generates GestureFlingStart instead.
            bool inertial;
            // True if this event was synthesized in order to generate the proper
            // GSB/GSU/GSE matching sequences. This is a temporary so that a future
            // GSB will generate a hit test so latching behavior is avoided
            // until crbug.com/526463 is fully implemented.
            bool synthetic;
        } scrollEnd;

        struct {
            float velocityX;
            float velocityY;
            // If true, this event will skip hit testing to find a scroll
            // target and instead just scroll the viewport.
            bool targetViewport;
        } flingStart;

        struct {
            // If set to true, don't treat flingCancel
            // as a part of fling boost events sequence.
            bool preventBoosting;
        } flingCancel;

        struct {
            bool zoomDisabled;
            float scale;
        } pinchUpdate;
    } data;

    WebGestureEvent()
        : WebInputEvent(sizeof(WebGestureEvent))
        , x(0)
        , y(0)
        , globalX(0)
        , globalY(0)
        , sourceDevice(WebGestureDeviceUninitialized)
        , resendingPluginId(-1)
    {
        memset(&data, 0, sizeof(data));
    }
};

// WebTouchEvent --------------------------------------------------------------

// TODO(e_hakkinen): Replace with WebPointerEvent. crbug.com/508283
class WebTouchEvent : public WebInputEvent {
public:
    // Maximum number of simultaneous touches supported on
    // Ash/Aura.
    enum { touchesLengthCap = 16 };

    unsigned touchesLength;
    // List of all touches, regardless of state.
    WebTouchPoint touches[touchesLengthCap];

    // Whether the event can be canceled (with preventDefault). If true then the browser
    // must wait for an ACK for this event. If false then no ACK IPC is expected.
    bool cancelable;

    // For a single touch, this is true after the touch-point has moved beyond
    // the platform slop region. For a multitouch, this is true after any
    // touch-point has moved (by whatever amount).
    bool movedBeyondSlopRegion;

    // A unique identifier for the touch event.
    uint32_t uniqueTouchEventId;

    WebTouchEvent()
        : WebInputEvent(sizeof(WebTouchEvent))
        , touchesLength(0)
        , cancelable(true)
        , movedBeyondSlopRegion(false)
        , uniqueTouchEventId(0)
    {
    }
};

#pragma pack(pop)

} // namespace blink

#endif
