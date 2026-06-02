/*---------------------------------------------------------*\
| OpenRGBWakeRetriggerPage.h                                |
|                                                           |
|   Settings page for the post-standby RGB retrigger.       |
|   Lets the user scan the system hardware fingerprint      |
|   (CPU + mainboard), tune the retrigger attempts/delay    |
|   and enable a scheduled task that re-applies the active  |
|   profile after the machine wakes from standby.           |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>

#include "WakeRetriggerConfig.h"

class OpenRGBWakeRetriggerPage : public QWidget
{
    Q_OBJECT

public:
    explicit OpenRGBWakeRetriggerPage(QWidget* parent = nullptr);
    ~OpenRGBWakeRetriggerPage();

private slots:
    void on_ScanClicked();
    void on_FieldsChanged();
    void on_FieldsCommitted();
    void on_RetriggerToggled(bool checked);
    void on_TestClicked();

private:
    void UpdateCheckboxState();
    void SaveConfig();

    QLineEdit*          cpu_input;
    QLineEdit*          board_input;
    QPushButton*        scan_button;
    QLineEdit*          attempts_input;
    QLineEdit*          delay_input;
    QCheckBox*          retrigger_checkbox;
    QPushButton*        test_button;

    WakeRetriggerConfig config;
};
