#pragma once

#include "action.h"
#include <QString>

namespace libgestures
{

/**
 * Invokes a Plasma global shortcut through DBus.
 */
class PlasmaGlobalShortcutGestureAction : public GestureAction
{
public:
    void execute() override;
    void setComponent(const QString &component);
    void setShortcut(const QString &shortcut);

private:
    QString m_path;
    QString m_shortcut;
};

}