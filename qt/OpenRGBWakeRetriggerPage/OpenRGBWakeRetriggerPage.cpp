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
#include "RGBController.h"
#include "WakeRetriggerVerify.h"
#include "WakeRetriggerRunner.h"
#include "WakeRetriggerTask.h"
#include "dmiinfo.h"

#define WAKE_RETRIGGER_PROFILE_NAME "WakeRetrigger"

#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
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
    test_button        = new QPushButton(tr("Test jetzt (Sequenz live ausfuehren)"), this);

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
    | The live test runs the scheduled sequence on demand.   |
    | It only makes sense once the fingerprint is present    |
    | and the standby task is enabled, so it starts disabled.|
    \*-----------------------------------------------------*/
    test_button->setEnabled(false);

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
    loop_form->addRow(test_button);

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
    connect(test_button,        &QPushButton::clicked,      this, &OpenRGBWakeRetriggerPage::on_TestClicked);

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

    /*-----------------------------------------------------*\
    | Refresh the live-test button (tied to the task state)  |
    \*-----------------------------------------------------*/
    UpdateCheckboxState();
}

void OpenRGBWakeRetriggerPage::on_TestClicked()
{
    /*-----------------------------------------------------*\
    | Build a non-modal log window for the test run          |
    \*-----------------------------------------------------*/
    QDialog* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Wake Retrigger - Test"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout*    dlg_layout = new QVBoxLayout(dlg);
    QPlainTextEdit* log_view   = new QPlainTextEdit(dlg);
    log_view->setReadOnly(true);
    log_view->setMinimumSize(560, 320);
    dlg_layout->addWidget(log_view);

    QPushButton* close_button = new QPushButton(tr("Schliessen"), dlg);
    close_button->setEnabled(false);
    dlg_layout->addWidget(close_button);
    connect(close_button, &QPushButton::clicked, dlg, &QDialog::accept);

    dlg->show();

    test_button->setEnabled(false);
    log_view->appendPlainText(tr("Starte Test - dieselbe Sequenz wie der geplante Task "
                                 "(Verifikation, Profil laden, Nudge-Apply pro Geraet)."));

    /*-----------------------------------------------------*\
    | Snapshot the live controllers and run the real         |
    | sequence on a worker thread so the UI stays responsive |
    | during the per-attempt delays.  Log lines and the      |
    | finished state are marshalled back to the GUI thread.  |
    \*-----------------------------------------------------*/
    std::vector<RGBController*> controllers = ResourceManager::get()->GetRGBControllers();

    QPointer<QPlainTextEdit> safe_log   = log_view;
    QPointer<QPushButton>    safe_close  = close_button;
    QPointer<QPushButton>    safe_test   = test_button;

    std::thread([controllers, safe_log, safe_close, safe_test]() mutable
    {
        auto post = [](QObject* target, std::function<void()> fn)
        {
            if(target != nullptr)
            {
                QMetaObject::invokeMethod(target, fn, Qt::QueuedConnection);
            }
        };

        auto sink = [safe_log, post](const std::string& line)
        {
            const QString qline = QString::fromStdString(line);
            post(safe_log, [safe_log, qline]()
            {
                if(safe_log)
                {
                    safe_log->appendPlainText(qline);
                }
            });
        };

        bool ok = WakeRetriggerRunner::Run(controllers, sink);

        const QString done = ok ? QObject::tr("Test erfolgreich abgeschlossen.")
                                : QObject::tr("Test fehlgeschlagen - siehe Meldung oben.");

        post(safe_log, [safe_log, done]()
        {
            if(safe_log)
            {
                safe_log->appendPlainText(done);
            }
        });
        post(safe_close, [safe_close]()
        {
            if(safe_close)
            {
                safe_close->setEnabled(true);
            }
        });
        post(safe_test, [safe_test]()
        {
            if(safe_test)
            {
                safe_test->setEnabled(true);
            }
        });
    }).detach();
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

    /*-----------------------------------------------------*\
    | The live test mirrors the scheduled task, so it is     |
    | only offered once CPU + mainboard are set AND the      |
    | standby retrigger is enabled.                          |
    \*-----------------------------------------------------*/
    test_button->setEnabled(ready && retrigger_checkbox->isChecked());
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
