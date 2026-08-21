#include "settings-dialog.hpp"

#include "wasapi-renderer.hpp"

#include <obs-module.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

namespace {
QString text(const char *key)
{
    return QString::fromUtf8(obs_module_text(key));
}
} // namespace

void show_settings_dialog(QWidget *parent, const std::string &active_device_id,
                          const ApplyDeviceCallback &apply_device)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(text("SettingsTitle"));
    dialog.setMinimumWidth(480);

    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(text("SettingsDescription"), &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *form = new QFormLayout();
    auto *device_row = new QHBoxLayout();
    auto *devices = new QComboBox(&dialog);
    auto *refresh = new QPushButton(text("Refresh"), &dialog);
    device_row->addWidget(devices, 1);
    device_row->addWidget(refresh);
    form->addRow(text("PlaybackDevice"), device_row);
    layout->addLayout(form);

    auto *status = new QLabel(&dialog);
    layout->addWidget(status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::Apply,
                                         &dialog);
    layout->addWidget(buttons);

    std::string selected_device_id = active_device_id;

    const auto populate_devices = [&]() {
        const QString previous_id = devices->currentData().toString();
        const QString preferred_id = previous_id.isEmpty()
                                         ? QString::fromUtf8(selected_device_id.c_str())
                                         : previous_id;

        devices->clear();
        const auto available_devices = WasapiRenderer::enumerate_devices();
        for (const WasapiDevice &device : available_devices) {
            devices->addItem(QString::fromUtf8(device.name.c_str()),
                             QString::fromUtf8(device.id.c_str()));
        }

        int selected_index = devices->findData(preferred_id);
        if (selected_index < 0) {
            for (int index = 0; index < devices->count(); ++index) {
                if (devices->itemText(index).contains(QStringLiteral("CABLE Input"),
                                                      Qt::CaseInsensitive)) {
                    selected_index = index;
                    break;
                }
            }
        }
        if (selected_index < 0 && devices->count() > 0)
            selected_index = 0;

        devices->setCurrentIndex(selected_index);
        const bool has_devices = selected_index >= 0;
        devices->setEnabled(has_devices);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(has_devices);
        buttons->button(QDialogButtonBox::Apply)->setEnabled(has_devices);
        status->setText(has_devices ? text("Ready") : text("NoPlaybackDevices"));
    };

    const auto apply_selection = [&]() -> bool {
        if (devices->currentIndex() < 0)
            return false;

        const std::string device_id = devices->currentData().toString().toUtf8().constData();
        if (!apply_device(device_id)) {
            QMessageBox::critical(&dialog, text("SettingsTitle"), text("ApplyFailed"));
            return false;
        }

        selected_device_id = device_id;
        status->setText(text("ApplySucceeded"));
        return true;
    };

    QObject::connect(refresh, &QPushButton::clicked, &dialog, populate_devices);
    QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog,
                     apply_selection);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (apply_selection())
            dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    populate_devices();
    dialog.exec();
}
