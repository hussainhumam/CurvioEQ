#include "presetpanelcontroller.h"

#include "ui/appconstants.h"
#include "ui/autoeqpresetsdialog.h"

#include <QFileDialog>
#include <QDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

PresetPanelController::PresetPanelController(QListWidget *listWidget,
                                             QPushButton *saveButton,
                                             QPushButton *importButton,
                                             QPushButton *autoEqButton,
                                             PresetStore *store,
                                             QObject *parent)
    : QObject(parent)
    , m_listWidget(listWidget)
    , m_saveButton(saveButton)
    , m_importButton(importButton)
    , m_autoEqButton(autoEqButton)
    , m_store(store)
{
    connect(m_saveButton, &QPushButton::clicked, this, &PresetPanelController::onSaveClicked);
    connect(m_importButton, &QPushButton::clicked, this, &PresetPanelController::onImportClicked);
    if (m_autoEqButton) {
        connect(m_autoEqButton, &QPushButton::clicked, this, &PresetPanelController::onAutoEqClicked);
    }
    connect(m_listWidget, &QListWidget::currentItemChanged, this, &PresetPanelController::onCurrentPresetChanged);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listWidget, &QListWidget::customContextMenuRequested,
            this, &PresetPanelController::onPresetContextMenu);
}

void PresetPanelController::setBandSliders(const std::array<QSlider *, EqProcessor::kBandCount> &sliders)
{
    m_bandSliders = sliders;
}

void PresetPanelController::setEqStateReader(std::function<EqState()> reader)
{
    m_eqStateReader = std::move(reader);
}

void PresetPanelController::setEqStateApplier(std::function<void(const EqState &)> applier)
{
    m_eqStateApplier = std::move(applier);
}

void PresetPanelController::refreshList()
{
    if (!m_listWidget || !m_store) {
        return;
    }

    const QString selectedId = selectedPresetId();
    m_updatingList = true;
    m_listWidget->clear();

    const QVector<EqPreset> builtIns = m_store->builtInPresets();
    for (int i = 0; i < builtIns.size(); ++i) {
        const EqPreset &preset = builtIns.at(i);
        if (i == AppConstants::kBuiltInGamingPresetSeparatorIndex) {
            auto *separator = new QListWidgetItem(QStringLiteral("— Gaming —"));
            separator->setFlags(Qt::NoItemFlags);
            separator->setData(Qt::UserRole, QString());
            m_listWidget->addItem(separator);
        }

        auto *item = new QListWidgetItem(displayNameForPreset(preset));
        item->setData(Qt::UserRole, preset.id);
        if (preset.eq.advanced) {
            item->setToolTip(QStringLiteral("Advanced parametric preset"));
        }
        m_listWidget->addItem(item);
    }

    const QVector<EqPreset> userPresets = m_store->userPresets();
    if (!userPresets.isEmpty()) {
        auto *separator = new QListWidgetItem(QStringLiteral("— Saved —"));
        separator->setFlags(Qt::NoItemFlags);
        separator->setData(Qt::UserRole, QString());
        m_listWidget->addItem(separator);

        for (const EqPreset &preset : userPresets) {
            auto *item = new QListWidgetItem(displayNameForPreset(preset));
            item->setData(Qt::UserRole, preset.id);
            if (preset.eq.advanced) {
                item->setToolTip(QStringLiteral("Advanced parametric preset"));
            }
            m_listWidget->addItem(item);
        }
    }

    selectPresetById(selectedId);
    m_updatingList = false;
}

void PresetPanelController::applyPresetToUi(const EqPreset &preset)
{
    if (m_eqStateApplier) {
        m_eqStateApplier(preset.eq);
    } else {
        for (int band = 0; band < EqProcessor::kBandCount; ++band) {
            const int value = qBound(-AppConstants::kMaxGainDb,
                                     qRound(preset.eq.gainsDb[static_cast<size_t>(band)]),
                                     AppConstants::kMaxGainDb);
            if (m_bandSliders[static_cast<size_t>(band)]) {
                m_bandSliders[static_cast<size_t>(band)]->setValue(value);
            }
        }
    }
}

void PresetPanelController::onSaveClicked()
{
    if (!m_store || !m_eqStateReader) {
        return;
    }

    const EqState currentEq = m_eqStateReader();
    const QString defaultName = currentEq.advanced ? QStringLiteral("My Advanced preset")
                                                   : QStringLiteral("My preset");

    bool ok = false;
    const QString name = QInputDialog::getText(m_listWidget,
                                               currentEq.advanced ? QStringLiteral("Save Advanced preset")
                                                                  : QStringLiteral("Save preset"),
                                               currentEq.advanced
                                                   ? QStringLiteral("Advanced preset name:")
                                                   : QStringLiteral("Preset name:"),
                                               QLineEdit::Normal,
                                               defaultName,
                                               &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    EqPreset created;
    if (!m_store->addUserPreset(name.trimmed(), currentEq, &created)) {
        emit errorOccurred(QStringLiteral("Save preset failed"),
                           QStringLiteral("Could not write presets to disk"));
        return;
    }

    refreshList();
    selectPresetById(created.id);
    emit logMessage(QStringLiteral("INFO"),
                    currentEq.advanced ? QStringLiteral("Saved Advanced preset: %1").arg(created.name)
                                       : QStringLiteral("Saved preset: %1").arg(created.name));
}

void PresetPanelController::onImportClicked()
{
    if (!m_store) {
        return;
    }

    const QString path = QFileDialog::getOpenFileName(m_listWidget,
                                                      QStringLiteral("Import preset"),
                                                      QString(),
                                                      QStringLiteral("Preset files (*.json);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!m_store->importFromFile(path, &errorMessage)) {
        emit errorOccurred(QStringLiteral("Import preset failed"), errorMessage);
        return;
    }

    refreshList();
    emit logMessage(QStringLiteral("INFO"), QStringLiteral("Imported preset(s) from %1").arg(path));
}

void PresetPanelController::onExportClicked()
{
    if (!m_store) {
        return;
    }

    const QString presetId = selectedPresetId();
    if (!isUserPresetId(presetId)) {
        emit logMessage(QStringLiteral("WARN"), QStringLiteral("Select a saved preset to export"));
        return;
    }

    const EqPreset preset = m_store->presetById(presetId);
    const QString path = QFileDialog::getSaveFileName(m_listWidget,
                                                      QStringLiteral("Export preset"),
                                                      preset.name + QStringLiteral(".json"),
                                                      QStringLiteral("Preset files (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!m_store->exportToFile(presetId, path, &errorMessage)) {
        emit errorOccurred(QStringLiteral("Export preset failed"), errorMessage);
        return;
    }

    emit logMessage(QStringLiteral("INFO"), QStringLiteral("Exported preset to %1").arg(path));
}

void PresetPanelController::onDeleteClicked()
{
    if (!m_store) {
        return;
    }

    const QString presetId = selectedPresetId();
    if (!isUserPresetId(presetId)) {
        return;
    }

    const EqPreset preset = m_store->presetById(presetId);
    const QMessageBox::StandardButton answer = QMessageBox::question(
        m_listWidget,
        QStringLiteral("Delete preset"),
        QStringLiteral("Delete preset \"%1\"?").arg(preset.name));
    if (answer != QMessageBox::Yes) {
        return;
    }

    if (!m_store->removeUserPreset(presetId)) {
        emit errorOccurred(QStringLiteral("Delete preset failed"),
                           QStringLiteral("Could not update presets on disk"));
        return;
    }

    refreshList();
    emit logMessage(QStringLiteral("INFO"), QStringLiteral("Deleted preset: %1").arg(preset.name));
}

void PresetPanelController::onAutoEqClicked()
{
    if (!m_store) {
        return;
    }

    QWidget *parentWidget = m_listWidget ? m_listWidget->window() : nullptr;
    OnlinePresetsDialog dialog(m_store, parentWidget);
    if (dialog.exec() != QDialog::Accepted || !dialog.didImport()) {
        return;
    }

    const EqPreset imported = dialog.importedPreset();
    refreshList();
    selectPresetById(imported.id);
    applyPresetToUi(imported);
    emit presetApplied(imported);
    emit logMessage(QStringLiteral("INFO"),
                    QStringLiteral("Imported AutoEQ preset: %1").arg(imported.name));
}

void PresetPanelController::onPresetContextMenu(const QPoint &pos)
{
    if (!m_listWidget || !m_store) {
        return;
    }

    QListWidgetItem *item = m_listWidget->itemAt(pos);
    if (!item) {
        return;
    }

    const QString presetId = item->data(Qt::UserRole).toString();
    if (presetId.isEmpty() || !isUserPresetId(presetId)) {
        return;
    }

    m_listWidget->setCurrentItem(item);

    QMenu menu(m_listWidget);
    QAction *exportAction = menu.addAction(QStringLiteral("Export…"));
    QAction *deleteAction = menu.addAction(QStringLiteral("Delete"));
    QAction *chosen = menu.exec(m_listWidget->viewport()->mapToGlobal(pos));
    if (chosen == exportAction) {
        onExportClicked();
    } else if (chosen == deleteAction) {
        onDeleteClicked();
    }
}

void PresetPanelController::onCurrentPresetChanged(QListWidgetItem *current, QListWidgetItem *)
{
    if (m_updatingList || !current || !m_store) {
        return;
    }

    const QString presetId = current->data(Qt::UserRole).toString();
    if (presetId.isEmpty()) {
        return;
    }

    const EqPreset preset = m_store->presetById(presetId);
    if (preset.id.isEmpty()) {
        return;
    }

    applyPresetToUi(preset);
    emit presetApplied(preset);
    emit logMessage(QStringLiteral("INFO"), QStringLiteral("Loaded preset: %1").arg(preset.name));
}

QString PresetPanelController::selectedPresetId() const
{
    if (!m_listWidget) {
        return {};
    }

    const QListWidgetItem *item = m_listWidget->currentItem();
    if (!item) {
        return {};
    }
    return item->data(Qt::UserRole).toString();
}

QString PresetPanelController::displayNameForPreset(const EqPreset &preset) const
{
    if (preset.eq.advanced) {
        return QStringLiteral("%1  · Advanced").arg(preset.name);
    }
    return preset.name;
}

bool PresetPanelController::isUserPresetId(const QString &id) const
{
    if (id.isEmpty() || !m_store) {
        return false;
    }

    for (const EqPreset &preset : m_store->userPresets()) {
        if (preset.id == id) {
            return true;
        }
    }
    return false;
}

void PresetPanelController::selectPresetById(const QString &presetId)
{
    if (!m_listWidget || presetId.isEmpty()) {
        return;
    }

    for (int row = 0; row < m_listWidget->count(); ++row) {
        QListWidgetItem *item = m_listWidget->item(row);
        if (item && item->data(Qt::UserRole).toString() == presetId) {
            m_listWidget->setCurrentItem(item);
            break;
        }
    }
}
