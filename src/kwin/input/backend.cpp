/*
    Input Actions - Input handler that executes user-defined actions
    Copyright (C) 2024-2025 Marcin Woźniak

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "backend.h"
#include "emitter.h"
#include "utils.h"

#include <libinputactions/input/emitter.h>
#include <libinputactions/input/events.h>
#include <libinputactions/triggers/stroke.h>

#ifndef KWIN_6_3_OR_GREATER
#include "core/inputdevice.h"
#endif
#include "input_event.h"
#include "input_event_spy.h"
#include "wayland_server.h"

#ifndef KWIN_6_2_OR_GREATER
#define ENSURE_SESSION_UNLOCKED()                  \
    if (KWin::waylandServer()->isScreenLocked()) { \
        return false;                              \
    }
#else
#define ENSURE_SESSION_UNLOCKED()
#endif

using namespace libinputactions;

static uint32_t s_strokeRecordingTimeout = 250;

KWinInputBackend::KWinInputBackend()
#ifdef KWIN_6_2_OR_GREATER
    : KWin::InputEventFilter(KWin::InputFilterOrder::TabBox)
#endif
{
}

bool KWinInputBackend::holdGestureBegin(int fingerCount, std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    const TouchpadGestureLifecyclePhaseEvent event(TouchpadGestureLifecyclePhase::Begin, TriggerType::Press, fingerCount);
    handleEvent(&event);
    return false;
}

bool KWinInputBackend::holdGestureEnd(std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    const TouchpadGestureLifecyclePhaseEvent event(TouchpadGestureLifecyclePhase::End, TriggerType::Press);
    if (handleEvent(&event)) {
        KWin::input()->processSpies([&time](auto &&spy) {
            spy->holdGestureCancelled(time);
        });
        KWin::input()->processFilters([&time](auto &&filter) {
            return filter->holdGestureCancelled(time);
        });
        return true;
    }
    return false;
}

bool KWinInputBackend::holdGestureCancelled(std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    const TouchpadGestureLifecyclePhaseEvent event(TouchpadGestureLifecyclePhase::Cancel, TriggerType::Press);
    handleEvent(&event);
    return false;
}

bool KWinInputBackend::swipeGestureBegin(int fingerCount, std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    if (m_isRecordingStroke) {
        return true;
    }

    const TouchpadGestureLifecyclePhaseEvent event(TouchpadGestureLifecyclePhase::Begin, TriggerType::StrokeSwipe, fingerCount);
    handleEvent(&event);
    return false;
}

bool KWinInputBackend::swipeGestureUpdate(const QPointF &delta, std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    if (m_isRecordingStroke) {
        m_strokePoints.push_back(delta);
        return true;
    }

    const MotionEvent event(delta, InputEventType::TouchpadSwipe);
    return handleEvent(&event);
}

bool KWinInputBackend::swipeGestureEnd(std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    if (m_isRecordingStroke) {
        finishStrokeRecording();
        return true;
    }

    const TouchpadGestureLifecyclePhaseEvent event(TouchpadGestureLifecyclePhase::End, TriggerType::StrokeSwipe);
    if (handleEvent(&event)) {
        KWin::input()->processSpies([&time](auto &&spy) {
            spy->swipeGestureCancelled(time);
        });
        KWin::input()->processFilters([&time](auto &&filter) {
            return filter->swipeGestureCancelled(time);
        });
        return true;
    }
    return false;
}

bool KWinInputBackend::swipeGestureCancelled(std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    const TouchpadGestureLifecyclePhaseEvent event(TouchpadGestureLifecyclePhase::Cancel, TriggerType::StrokeSwipe);
    handleEvent(&event);
    return false;
}

bool KWinInputBackend::pinchGestureBegin(int fingerCount, std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    m_pinchGestureActive = true;
    const TouchpadGestureLifecyclePhaseEvent event(TouchpadGestureLifecyclePhase::Begin, TriggerType::PinchRotate, fingerCount);
    handleEvent(&event);
    return false;
}

bool KWinInputBackend::pinchGestureUpdate(qreal scale, qreal angleDelta, const QPointF &delta, std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    // Two-finger pinch gestures may be at first incorrectly interpreted by libinput as scrolling. Libinput does
    // eventually correctly determine the gesture after a few events, but it doesn't send the GESTURE_PINCH_BEGIN event.
    if (!m_pinchGestureActive) {
        pinchGestureBegin(2, time);
    }

    const TouchpadPinchEvent event(scale, angleDelta);
    return handleEvent(&event);
}

bool KWinInputBackend::pinchGestureEnd(std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    m_pinchGestureActive = false;
    const TouchpadGestureLifecyclePhaseEvent event(TouchpadGestureLifecyclePhase::End, TriggerType::PinchRotate);
    if (handleEvent(&event)) {
        KWin::input()->processSpies([&time](auto &&spy) {
            spy->pinchGestureCancelled(time);
        });
        KWin::input()->processFilters([&time](auto &&filter) {
            return filter->pinchGestureCancelled(time);
        });
        return true;
    }
    return false;
}

bool KWinInputBackend::pinchGestureCancelled(std::chrono::microseconds time)
{
    ENSURE_SESSION_UNLOCKED();

    m_pinchGestureActive = false;
    const TouchpadGestureLifecyclePhaseEvent event(TouchpadGestureLifecyclePhase::Cancel, TriggerType::PinchRotate);
    handleEvent(&event);
    return false;
}

#ifdef KWIN_6_3_OR_GREATER
bool KWinInputBackend::pointerMotion(KWin::PointerMotionEvent *event)
{
    const auto device = event->device;
    if (m_ignoreEvents || !device || !isMouse(event->device)) {
        return false;
    }

    if (m_isRecordingStroke) {
        m_strokePoints.push_back(event->delta);
        m_strokeRecordingTimeoutTimer.start(s_strokeRecordingTimeout);
    } else {
        const MotionEvent motionEvent(event->delta, InputEventType::MouseMotion, device->name());
        handleEvent(&motionEvent);
    }
    return false;
}

bool KWinInputBackend::pointerButton(KWin::PointerButtonEvent *event)
{
    if (m_ignoreEvents || !event->device || !isMouse(event->device)) {
        return false;
    }

    const MouseButtonEvent buttonEvent(event->button, event->nativeButton, event->state == PointerButtonStatePressed, event->device->name());
    return handleEvent(&buttonEvent);
}

bool KWinInputBackend::keyboardKey(KWin::KeyboardKeyEvent *event)
{
    if (m_ignoreEvents || !event->device) {
        return false;
    }

    const KeyboardKeyEvent keyEvent(event->nativeScanCode, event->state == KeyboardKeyStatePressed);
    handleEvent(&keyEvent);
    return false;
}
#endif

#ifdef KWIN_6_3_OR_GREATER
bool KWinInputBackend::pointerAxis(KWin::PointerAxisEvent *event)
{
    const auto device = event->device;
    const auto eventDelta = event->delta;
    const auto orientation = event->orientation;
    const auto inverted = event->inverted;
#else
bool KWinInputBackend::wheelEvent(KWin::WheelEvent *event)
{
    const auto device = event->device();
    const auto eventDelta = event->delta();
    const auto orientation = event->orientation();
    const auto inverted = event->inverted();
#endif

    ENSURE_SESSION_UNLOCKED();

    const auto mouse = isMouse(device);
    if (m_ignoreEvents || (!device->isTouchpad() && !mouse)) {
        return false;
    }

    auto delta = orientation == Qt::Orientation::Horizontal
        ? QPointF(eventDelta, 0)
        : QPointF(0, eventDelta);
    if (inverted) {
        delta *= -1;
    }

    if (mouse) {
        const MotionEvent wheelEvent(delta, InputEventType::MouseWheel, device->name());
        return handleEvent(&wheelEvent);
    }

    if (m_isRecordingStroke) {
        m_strokePoints.push_back(delta);
        m_strokeRecordingTimeoutTimer.start(s_strokeRecordingTimeout);
        return true;
    }

    const MotionEvent scrollEvent(delta, InputEventType::TouchpadScroll);
    return handleEvent(&scrollEvent);
}

bool KWinInputBackend::isMouse(const KWin::InputDevice *device) const
{
    return device->isPointer() && !device->isTouch() && !device->isTouchpad();
}