#include "presetpanelcontroller.h"

#include "ui/appconstants.h"
#include "ui/autoeqpresetsdialog.h"
#include "ui/savepresetdialog.h"

#include <QColor>
#include <QFileDialog>
#include <QDialog>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QWidget>
#include <QtMath>

namespace {

constexpr int kStarRadius = 8;
constexpr int kStarMargin = 10;
constexpr int kStarGutter = kStarMargin + kStarRadius * 2 + 6;
const QColor kStarColor(255, 204, 0);

QPainterPath starPath(const QPointF &center, qreal outerRadius)
{
    QPainterPath path;
    const qreal innerRadius = outerRadius * 0.45;
    for (int i = 0; i < 10; ++i) {
        const qreal radius = (i % 2 == 0) ? outerRadius : innerRadius;
        const qreal angle = qDegreesToRadians(-90.0 + i * 36.0);
        const QPointF point(center.x() + radius * qCos(angle),
                            center.y() + radius * qSin(angle));
        if (i == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
    }
    path.closeSubpath();
    return path;
}

class PresetItemDelegate : public QStyledItemDelegate
{
public:
    explicit PresetItemDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const bool favorite = index.data(PresetPanelController::RoleFavorite).toBool();

        QStyleOptionViewItem textOption(option);
        if (favorite) {
            textOption.rect.adjust(0, 0, -kStarGutter, 0);
        }
        QStyledItemDelegate::paint(painter, textOption, index);

        if (!favorite) {
            return;
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(kStarColor);
        const QPoint center(option.rect.right() - kStarMargin - kStarRadius,
                            option.rect.center().y());
        painter->drawPath(starPath(center, kStarRadius));
        painter->restore();
    }
};

} // namespace

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
    m_listWidget->setItemDelegate(new PresetItemDelegate(m_listWidget));
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

void PresetPanelController::setSurroundStateReader(std::function<VirtualSurroundSettings()> reader)
{
    m_surroundStateReader = std::move(reader);
}

void PresetPanelController::setSurroundStateApplier(std::function<void(const VirtualSurroundSettings &)> applier)
{
    m_surroundStateApplier = std::move(applier);
}

void PresetPanelController::setDynamicsStateReader(std::function<DynamicRangeSettings()> reader)
{
    m_dynamicsStateReader = std::move(reader);
}

void PresetPanelController::setDynamicsStateApplier(std::function<void(const DynamicRangeSettings &)> applier)
{
    m_dynamicsStateApplier = std::move(applier);
}

void PresetPanelController::setAudioChainOrderReader(std::function<AudioChainOrder()> reader)
{
    m_audioChainOrderReader = std::move(reader);
}

void PresetPanelController::setAudioChainOrderApplier(std::function<void(const AudioChainOrder &)> applier)
{
    m_audioChainOrderApplier = std::move(applier);
}

void PresetPanelController::refreshList()
{
    if (!m_listWidget || !m_store) {
        return;
    }

    const QString selectedId = selectedPresetId();
    m_updatingList = true;
    m_listWidget->clear();

    QSet<QString> shownIds;
    const QStringList favoriteIds = m_store->favoriteIds();
    bool favoritesHeaderAdded = false;
    for (const QString &id : favoriteIds) {
        const EqPreset preset = m_store->presetById(id);
        if (preset.id.isEmpty() || shownIds.contains(preset.id)) {
            continue;
        }
        if (!favoritesHeaderAdded) {
            auto *separator = new QListWidgetItem(QStringLiteral("— Favorites —"));
            separator->setFlags(Qt::NoItemFlags);
            separator->setData(Qt::UserRole, QString());
            m_listWidget->addItem(separator);
            favoritesHeaderAdded = true;
        }
        addPresetItem(preset);
        shownIds.insert(preset.id);
    }

    const QVector<EqPreset> builtIns = m_store->builtInPresets();
    bool genericHeaderAdded = false;
    for (int i = 0; i < builtIns.size(); ++i) {
        const EqPreset &preset = builtIns.at(i);
        if (i == AppConstants::kBuiltInGamingPresetSeparatorIndex) {
            auto *separator = new QListWidgetItem(QStringLiteral("— Gaming —"));
            separator->setFlags(Qt::NoItemFlags);
            separator->setData(Qt::UserRole, QString());
            m_listWidget->addItem(separator);
        }

        if (shownIds.contains(preset.id)) {
            continue;
        }
        if (i < AppConstants::kBuiltInGamingPresetSeparatorIndex && !genericHeaderAdded) {
            auto *separator = new QListWidgetItem(QStringLiteral("— Generic —"));
            separator->setFlags(Qt::NoItemFlags);
            separator->setData(Qt::UserRole, QString());
            m_listWidget->addItem(separator);
            genericHeaderAdded = true;
        }
        addPresetItem(preset);
        shownIds.insert(preset.id);
    }

    const QVector<EqPreset> userPresets = m_store->userPresets();
    bool savedHeaderAdded = false;
    for (const EqPreset &preset : userPresets) {
        if (shownIds.contains(preset.id)) {
            continue;
        }
        if (!savedHeaderAdded) {
            auto *separator = new QListWidgetItem(QStringLiteral("— Saved —"));
            separator->setFlags(Qt::NoItemFlags);
            separator->setData(Qt::UserRole, QString());
            m_listWidget->addItem(separator);
            savedHeaderAdded = true;
        }
        addPresetItem(preset);
        shownIds.insert(preset.id);
    }

    selectPresetById(selectedId);
    m_updatingList = false;
}

void PresetPanelController::applyPresetToUi(const EqPreset &preset)
{
    if (preset.hasEq) {
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
    if (preset.hasSurround && m_surroundStateApplier) {
        m_surroundStateApplier(preset.surround);
    }
    if (preset.hasDynamics && m_dynamicsStateApplier) {
        m_dynamicsStateApplier(preset.dynamics);
    }
    if (preset.hasAudioChain && m_audioChainOrderApplier) {
        m_audioChainOrderApplier(preset.audioChainOrder);
    }
}

void PresetPanelController::markDirty()
{
    m_dirty = true;
}

void PresetPanelController::markClean(const QString &presetId)
{
    m_cleanPresetId = presetId;
    m_dirty = presetId.isEmpty();
}

bool PresetPanelController::ensureNamedPreset(EqPreset *outPreset)
{
    if (!m_dirty && !m_cleanPresetId.isEmpty() && m_store) {
        const EqPreset preset = m_store->presetById(m_cleanPresetId);
        if (!preset.id.isEmpty()) {
            if (outPreset) {
                *outPreset = preset;
            }
            return true;
        }
    }
    return saveCurrentPresetInteractive(outPreset);
}

bool PresetPanelController::saveCurrentPresetInteractive(EqPreset *createdPreset)
{
    if (!m_store) {
        return false;
    }

    const EqState currentEq = m_eqStateReader ? m_eqStateReader() : EqState{};
    const QString defaultName = currentEq.advanced ? QStringLiteral("My Advanced preset")
                                                   : QStringLiteral("My preset");

    SavePresetDialog dialog(defaultName, currentEq.advanced,
                            m_listWidget ? m_listWidget->window() : nullptr);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    EqPreset preset;
    preset.name = dialog.presetName();
    preset.hasEq = dialog.includeEq();
    preset.hasSurround = dialog.includeSurround();
    preset.hasDynamics = dialog.includeDynamics();
    preset.hasAudioChain = dialog.includeAudioChain();
    if (preset.hasEq) {
        if (!m_eqStateReader) {
            return false;
        }
        preset.eq = currentEq;
    }
    if (preset.hasSurround) {
        if (!m_surroundStateReader) {
            return false;
        }
        preset.surround = m_surroundStateReader();
    }
    if (preset.hasDynamics) {
        if (!m_dynamicsStateReader) {
            return false;
        }
        preset.dynamics = m_dynamicsStateReader();
    }
    if (preset.hasAudioChain) {
        if (!m_audioChainOrderReader) {
            return false;
        }
        preset.audioChainOrder = m_audioChainOrderReader();
    }

    EqPreset created;
    if (!m_store->addUserPreset(preset, &created)) {
        emit errorOccurred(QStringLiteral("Save preset failed"),
                           QStringLiteral("Could not write presets to disk"));
        return false;
    }

    refreshList();
    selectPresetById(created.id);
    markClean(created.id);
    const QString sections = PresetStore::includedSectionsLabel(created);
    emit logMessage(QStringLiteral("INFO"),
                    QStringLiteral("Saved preset: %1 (%2)").arg(created.name, sections));
    if (createdPreset) {
        *createdPreset = created;
    }
    return true;
}

void PresetPanelController::onSaveClicked()
{
    saveCurrentPresetInteractive(nullptr);
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

    if (m_cleanPresetId == presetId) {
        markClean({});
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
    markClean(imported.id);
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
    if (presetId.isEmpty()) {
        return;
    }

    m_listWidget->setCurrentItem(item);

    const bool favorite = m_store->isFavorite(presetId);
    QMenu menu(m_listWidget);
    QAction *favoriteAction = menu.addAction(favorite ? QStringLiteral("Remove from favorite")
                                                      : QStringLiteral("Add to favorite"));
    QAction *exportAction = nullptr;
    QAction *deleteAction = nullptr;
    if (isUserPresetId(presetId)) {
        menu.addSeparator();
        exportAction = menu.addAction(QStringLiteral("Export…"));
        deleteAction = menu.addAction(QStringLiteral("Delete"));
    }

    QAction *chosen = menu.exec(m_listWidget->viewport()->mapToGlobal(pos));
    if (chosen == favoriteAction) {
        onToggleFavorite(presetId);
    } else if (exportAction && chosen == exportAction) {
        onExportClicked();
    } else if (deleteAction && chosen == deleteAction) {
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
    markClean(preset.id);
    emit presetApplied(preset);
    const QString sections = PresetStore::includedSectionsLabel(preset);
    emit logMessage(QStringLiteral("INFO"),
                    QStringLiteral("Loaded preset: %1 (%2)").arg(preset.name, sections));
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
    if (preset.isBuiltIn) {
        return preset.name;
    }

    const QString sections = PresetStore::includedSectionsLabel(preset);
    if (preset.hasEq && preset.eq.advanced) {
        return QStringLiteral("%1  · Advanced · %2").arg(preset.name, sections);
    }
    return QStringLiteral("%1  · %2").arg(preset.name, sections);
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

void PresetPanelController::addPresetItem(const EqPreset &preset)
{
    auto *item = new QListWidgetItem(displayNameForPreset(preset));
    item->setData(Qt::UserRole, preset.id);
    item->setData(RoleFavorite, m_store && m_store->isFavorite(preset.id));
    const QString sections = PresetStore::includedSectionsLabel(preset);
    if (!sections.isEmpty()) {
        item->setToolTip(QStringLiteral("Includes: %1").arg(sections));
    }
    m_listWidget->addItem(item);
}

void PresetPanelController::onToggleFavorite(const QString &presetId)
{
    if (!m_store || presetId.isEmpty()) {
        return;
    }

    const bool makeFavorite = !m_store->isFavorite(presetId);
    if (!m_store->setFavorite(presetId, makeFavorite)) {
        emit errorOccurred(QStringLiteral("Favorite preset failed"),
                           QStringLiteral("Could not update presets on disk"));
        return;
    }

    refreshList();
    selectPresetById(presetId);
    const EqPreset preset = m_store->presetById(presetId);
    emit logMessage(QStringLiteral("INFO"),
                    makeFavorite ? QStringLiteral("Added favorite: %1").arg(preset.name)
                                 : QStringLiteral("Removed favorite: %1").arg(preset.name));
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
