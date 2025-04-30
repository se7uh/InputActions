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

#pragma once

#include "handler.h"

#include <QTimer>

namespace libinputactions
{

class Stroke;

/**
 * Collects input events and forwards them to handlers.
 */
class InputBackend : public QObject
{
    Q_OBJECT

public:
    void recordStroke();

    void addEventHandler(std::unique_ptr<InputEventHandler> handler);
    void clearEventHandlers();

    void setIgnoreEvents(const bool &value);

    static InputBackend *instance();
    static void setInstance(std::unique_ptr<InputBackend> instance);

signals:
    void strokeRecordingFinished(const Stroke &stroke);

protected:
    InputBackend();

    /**
     * @return Whether the event should be blocked.
     */
    bool handleEvent(const InputEvent *event);

    void finishStrokeRecording();

    std::vector<std::unique_ptr<InputEventHandler>> m_handlers;
    bool m_ignoreEvents{};

    bool m_isRecordingStroke = false;
    std::vector<QPointF> m_strokePoints;
    QTimer m_strokeRecordingTimeoutTimer;

private:
    static std::unique_ptr<InputBackend> s_instance;
};

}