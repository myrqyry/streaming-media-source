#pragma once
#include <QString>
void registerMediaDock(const QString &bundle, const QString &managed);
void unregisterMediaDock();
class QWidget;
QWidget *createMediaDockWidget(const QString &bundle = {},
                               const QString &managed = {});
