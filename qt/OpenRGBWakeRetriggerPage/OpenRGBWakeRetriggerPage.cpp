/*---------------------------------------------------------*\
| OpenRGBWakeRetriggerPage.cpp                              |
|                                                           |
|   Settings page for the post-standby RGB retrigger.       |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "OpenRGBWakeRetriggerPage.h"
#include "ResourceManager.h"
#include "ProfileManager.h"
#include "WakeRetriggerVerify.h"
#include "WakeRetriggerTask.h"
#include "dmiinfo.h"

#define WAKE_RETRIGGER_PROFILE_NAME "WakeRetrigger"

#include <QFormLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

OpenRGBWakeRetriggerPage::OpenRGBWakeRetriggerPage(QWidget* parent) : QWidget(parent)
{
    /*-----------------------------------------------------*\
    | Load the stored configuration                         |
    \*-----------------------------------------------------*/
    config.Load(ResourceManager::get()->GetSettingsManager());

    /*-----------------------------------------------------*\
    | Build the widgets                                     |
    \*-----------------------------------------------------*/
    cpu_input          = new QLineEdit(this);
    board_input        = new QLineEdit(this);
    scan_button        = new QPushButton(tr("System scannen"), this);
    attempts_input     = new QLineEdit(this);
    delay_input        = new QLineEdit(this);
    retrigger_checkbox = new QCheckBox(tr("Nach Standby retrigger via Aufgabenplanung"), this);

    attempts_input->setValidator(new QIntValidator(1, 9999, this));
    delay_input->setValidator(new QIntValidator(0, 9999, this));

    /*-----------------------------------------------------*\
    | Populate fields from the stored configuration         |
    | (before connecting signals to avoid spurious saves)   |
    \*-----------------------------------------------------*/
    cpu_input->setText(QString::fromStdString(config.cpu_string));
    board_input->setText(QString::fromStdString(config.board_string));
    attempts_input->setText(QString::number(config.attempts));
    delay_input->setText(QString::number(config.delay));

    /*-----------------------------------------------------*\
    | The retrigger task can only be enabled once a valid    |
    | hardware fingerprint is present.  Start disabled.     |
    \*-----------------------------------------------------*/
    retrigger_checkbox->setEnabled(false);
    retrigger_checkbox->setChecked(config.enabled);

    /*-----------------------------------------------------*\
    | Lay out the hardware fingerprint group                |
    \*-----------------------------------------------------*/
    QFormLayout* hw_form = new QFormLayout();
    hw_form->addRow(tr("CPU:"), cpu_input);
    hw_form->addRow(tr("Mainboard:"), board_input);
    hw_form->addRow("", scan_button);

    QGroupBox* hw_group = new QGroupBox(tr("Hardware-Erkennung"), this);
    hw_group->setLayout(hw_form);

    /*-----------------------------------------------------*\
    | Lay out the retrigger parameters group                |
    \*-----------------------------------------------------*/
    QFormLayout* loop_form = new QFormLayout();
    loop_form->addRow(tr("Versuche:"), attempts_input);
    loop_form->addRow(tr("Verzoegerung (s):"), delay_input);
    loop_form->addRow(retrigger_checkbox);

    QGroupBox* loop_group = new QGroupBox(tr("Retrigger nach Standby"), this);
    loop_group->setLayout(loop_form);

    /*-----------------------------------------------------*\
    | Top-level layout                                      |
    \*-----------------------------------------------------*/
    QLabel* info = new QLabel(tr("Erkennt die Hardware und re-appliziert das aktive Profil "
                                 "nach dem Aufwachen aus dem Standby. Die Aufgabenplanung "
                                 "kann erst aktiviert werden, wenn CPU und Mainboard gesetzt sind."), this);
    info->setWordWrap(true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(info);
    layout->addWidget(hw_group);
    layout->addWidget(loop_group);
    layout->addStretch(1);

    setLayout(layout);

    /*-----------------------------------------------------*\
    | Connect signals                                       |
    \*-----------------------------------------------------*/
    connect(scan_button,        &QPushButton::clicked,      this, &OpenRGBWakeRetriggerPage::on_ScanClicked);
    connect(cpu_input,          &QLineEdit::textChanged,    this, &OpenRGBWakeRetriggerPage::on_FieldsChanged);
    connect(board_input,        &QLineEdit::textChanged,    this, &OpenRGBWakeRetriggerPage::on_FieldsChanged);
    connect(cpu_input,          &QLineEdit::editingFinished, this, &OpenRGBWakeRetriggerPage::on_FieldsCommitted);
    connect(board_input,        &QLineEdit::editingFinished, this, &OpenRGBWakeRetriggerPage::on_FieldsCommitted);
    connect(attempts_input,     &QLineEdit::editingFinished, this, &OpenRGBWakeRetriggerPage::on_FieldsCommitted);
    connect(delay_input,        &QLineEdit::editingFinished, this, &OpenRGBWakeRetriggerPage::on_FieldsCommitted);
    connect(retrigger_checkbox, &QCheckBox::toggled,        this, &OpenRGBWakeRetriggerPage::on_RetriggerToggled);

    /*-----------------------------------------------------*\
    | Apply the initial enabled state                       |
    \*-----------------------------------------------------*/
    UpdateCheckboxState();
}

OpenRGBWakeRetriggerPage::~OpenRGBWakeRetriggerPage()
{
}

void OpenRGBWakeRetriggerPage::on_ScanClicked()
{
    DMIInfo dmi;

    std::string board = dmi.getManufacturer();

    if(!dmi.getProductName().empty())
    {
        if(!board.empty())
        {
            board += " ";
        }
        board += dmi.getProductName();
    }

    cpu_input->setText(QString::fromStdString(dmi.getProcessor()));
    board_input->setText(QString::fromStdString(board));

    /*-----------------------------------------------------*\
    | Capture the read-only SMBus fingerprint of the         |
    | currently installed DRAM alongside the CPU/board so    |
    | the verification has a reference to compare against.   |
    \*-----------------------------------------------------*/
    config.smbus_fingerprint = WakeRetriggerVerify::ComputeSMBusFingerprint();

    UpdateCheckboxState();
    SaveConfig();
}

void OpenRGBWakeRetriggerPage::on_FieldsChanged()
{
    UpdateCheckboxState();
}

void OpenRGBWakeRetriggerPage::on_FieldsCommitted()
{
    SaveConfig();
}

void OpenRGBWakeRetriggerPage::on_RetriggerToggled(bool checked)
{
    if(checked)
    {
        /*-------------------------------------------------*\
        | Capture a fingerprint if the user enabled the task |
        | without scanning first                             |
        \*-------------------------------------------------*/
        if(config.smbus_fingerprint.empty())
        {
            config.smbus_fingerprint = WakeRetriggerVerify::ComputeSMBusFingerprint();
        }

        /*-------------------------------------------------*\
        | Snapshot the current device state into a dedicated |
        | profile that the scheduled task re-applies on wake |
        \*-------------------------------------------------*/
        ResourceManager::get()->GetProfileManager()->SaveProfile(WAKE_RETRIGGER_PROFILE_NAME);
        config.profile_name = WAKE_RETRIGGER_PROFILE_NAME;

        SaveConfig();

        /*-------------------------------------------------*\
        | Register the scheduled task.  If that fails, show  |
        | the schtasks output and revert the checkbox.       |
        \*-------------------------------------------------*/
        std::string task_error;

        if(!WakeRetriggerTask::Enable(config.profile_name, task_error))
        {
            retrigger_checkbox->blockSignals(true);
            retrigger_checkbox->setChecked(false);
            retrigger_checkbox->blockSignals(false);

            config.enabled = false;
            SaveConfig();

            QMessageBox::warning(this, tr("Wake Retrigger"),
                tr("Die geplante Aufgabe konnte nicht erstellt werden.\n\n"
                   "Bitte OpenRGB als Administrator starten. Falls das Problem "
                   "weiterhin auftritt, meldet schtasks:\n\n%1")
                    .arg(QString::fromStdString(task_error)));
        }
    }
    else
    {
        std::string task_error;
        WakeRetriggerTask::Disable(task_error);
        SaveConfig();
    }
}

void OpenRGBWakeRetriggerPage::UpdateCheckboxState()
{
    bool ready = !cpu_input->text().trimmed().isEmpty()
              && !board_input->text().trimmed().isEmpty();

    retrigger_checkbox->setEnabled(ready);

    /*-----------------------------------------------------*\
    | Without a fingerprint the task must not stay enabled  |
    \*-----------------------------------------------------*/
    if(!ready && retrigger_checkbox->isChecked())
    {
        retrigger_checkbox->setChecked(false);
    }
}

void OpenRGBWakeRetriggerPage::SaveConfig()
{
    config.cpu_string   = cpu_input->text().trimmed().toStdString();
    config.board_string = board_input->text().trimmed().toStdString();

    unsigned int attempts = attempts_input->text().toUInt();

    if(attempts < 1)
    {
        attempts = 1;
    }

    config.attempts = attempts;
    config.delay    = delay_input->text().toUInt();
    config.enabled  = retrigger_checkbox->isChecked();

    config.Save(ResourceManager::get()->GetSettingsManager());
}
