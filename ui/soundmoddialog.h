#pragma once



#include "soundmods/soundmodtypes.h"



#include <QDialog>

#include <QHash>

#include <QVector>



class QCheckBox;

class QLabel;

class QLineEdit;

class QProgressBar;

class QPushButton;

class QSlider;

class QTreeWidget;

class QTreeWidgetItem;



class SoundModDialog : public QDialog

{

    Q_OBJECT



public:

    explicit SoundModDialog(unsigned long processId, const QString &displayName, QWidget *parent = nullptr);



private slots:

    void onBrowseRoot();

    void onScan();

    void onApply();

    void onRestoreAll();

    void onRestoreSelected();

    void onSelectionChanged();

    void onGainSliderChanged(int value);

    void onSearchChanged(const QString &text);



private:

    void buildUi();

    void mergeProfileIntoAssets();

    void populateTree();

    void updateScanSummary();

    void updateSelectedGainControls();

    QTreeWidgetItem *ensureFolderNode(const QString &relativeFolderPath);

    bool isFileItem(const QTreeWidgetItem *item) const;

    QString selectedFileRelativePath() const;

    QVector<SoundAssetEntry> currentAssets() const;

    bool confirmRunningProcessWarning() const;

    void showFirstRunWarningIfNeeded();



    unsigned long m_processId = 0;

    GameIdentity m_game;

    std::vector<SoundAssetEntry> m_assets;

    QStringList m_folderPaths;

    int m_scannedFileCount = 0;

    int m_scannedFolderCount = 0;

    bool m_scanning = false;



    QLineEdit *m_scanRootEdit = nullptr;

    QLineEdit *m_searchEdit = nullptr;

    QTreeWidget *m_tree = nullptr;

    QTreeWidgetItem *m_treeRoot = nullptr;

    QHash<QString, QTreeWidgetItem *> m_folderNodes;

    QSlider *m_gainSlider = nullptr;

    QCheckBox *m_enabledCheckBox = nullptr;

    QLabel *m_gainValueLabel = nullptr;

    QLabel *m_scanSummaryLabel = nullptr;

    QProgressBar *m_progressBar = nullptr;

    QPushButton *m_applyButton = nullptr;

    QPushButton *m_restoreAllButton = nullptr;

    QPushButton *m_restoreSelectedButton = nullptr;

};


