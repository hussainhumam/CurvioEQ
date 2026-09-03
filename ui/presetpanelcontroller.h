#pragma once

#include "audio/audiochainorder.h"
#include "audio/dynamicrangesettings.h"
#include "audio/eqprocessor.h"
#include "audio/eqstate.h"
#include "audio/virtualsurroundsettings.h"
#include "ui/presetstore.h"

#include <QObject>
#include <QPoint>

#include <array>
#include <functional>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;

class PresetPanelController : public QObject
{
    Q_OBJECT

public:
    PresetPanelController(QListWidget *listWidget,
                          QPushButton *saveButton,
                          QPushButton *importButton,
                          QPushButton *autoEqButton,
                          PresetStore *store,
                          QObject *parent = nullptr);

    void setBandSliders(const std::array<QSlider *, EqProcessor::kBandCount> &sliders);
    void setEqStateReader(std::function<EqState()> reader);
    void setEqStateApplier(std::function<void(const EqState &)> applier);
    void setSurroundStateReader(std::function<VirtualSurroundSettings()> reader);
    void setSurroundStateApplier(std::function<void(const VirtualSurroundSettings &)> applier);
    void setDynamicsStateReader(std::function<DynamicRangeSettings()> reader);
    void setDynamicsStateApplier(std::function<void(const DynamicRangeSettings &)> applier);
    void setAudioChainOrderReader(std::function<AudioChainOrder()> reader);
    void setAudioChainOrderApplier(std::function<void(const AudioChainOrder &)> applier);

    void refreshList();
    void applyPresetToUi(const EqPreset &preset);

    static constexpr int RoleFavorite = Qt::UserRole + 1;

signals:
    void presetApplied(const EqPreset &preset);
    void logMessage(const QString &level, const QString &message);
    void errorOccurred(const QString &title, const QString &message);

private slots:
    void onSaveClicked();
    void onImportClicked();
    void onExportClicked();
    void onDeleteClicked();
    void onAutoEqClicked();
    void onCurrentPresetChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onPresetContextMenu(const QPoint &pos);

private:
    QString selectedPresetId() const;
    QString displayNameForPreset(const EqPreset &preset) const;
    bool isUserPresetId(const QString &id) const;
    void selectPresetById(const QString &presetId);
    void addPresetItem(const EqPreset &preset);
    void onToggleFavorite(const QString &presetId);

    QListWidget *m_listWidget = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_autoEqButton = nullptr;
    PresetStore *m_store = nullptr;
    std::array<QSlider *, EqProcessor::kBandCount> m_bandSliders{};
    std::function<EqState()> m_eqStateReader;
    std::function<void(const EqState &)> m_eqStateApplier;
    std::function<VirtualSurroundSettings()> m_surroundStateReader;
    std::function<void(const VirtualSurroundSettings &)> m_surroundStateApplier;
    std::function<DynamicRangeSettings()> m_dynamicsStateReader;
    std::function<void(const DynamicRangeSettings &)> m_dynamicsStateApplier;
    std::function<AudioChainOrder()> m_audioChainOrderReader;
    std::function<void(const AudioChainOrder &)> m_audioChainOrderApplier;
    bool m_updatingList = false;
};
