#pragma once

#include "audio/eqprocessor.h"
#include "ui/presetstore.h"

#include <QObject>

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
                          QPushButton *exportButton,
                          QPushButton *deleteButton,
                          PresetStore *store,
                          QObject *parent = nullptr);

    void setBandSliders(const std::array<QSlider *, EqProcessor::kBandCount> &sliders);
    void setGainReader(std::function<std::array<float, EqProcessor::kBandCount>()> reader);
    void setEngineGainApplier(std::function<void(const std::array<float, EqProcessor::kBandCount> &)> applier);

    void refreshList();
    void applyPresetToSliders(const EqPreset &preset);

signals:
    void presetApplied(const EqPreset &preset);
    void logMessage(const QString &level, const QString &message);
    void errorOccurred(const QString &title, const QString &message);

private slots:
    void onSaveClicked();
    void onImportClicked();
    void onExportClicked();
    void onDeleteClicked();
    void onCurrentPresetChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onSelectionChanged();

private:
    QString selectedPresetId() const;
    bool isUserPresetId(const QString &id) const;
    void selectPresetById(const QString &presetId);

    QListWidget *m_listWidget = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_exportButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    PresetStore *m_store = nullptr;
    std::array<QSlider *, EqProcessor::kBandCount> m_bandSliders{};
    std::function<std::array<float, EqProcessor::kBandCount>()> m_gainReader;
    std::function<void(const std::array<float, EqProcessor::kBandCount> &)> m_engineGainApplier;
    bool m_updatingList = false;
};
