#include "audiochaindialog.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString stageLabel(AudioChainStage stage)
{
    switch (stage) {
    case AudioChainStage::Eq:
        return QStringLiteral("EQ");
    case AudioChainStage::VirtualSurround:
        return QStringLiteral("Virtual Surround");
    case AudioChainStage::Dynamics:
        return QStringLiteral("Dynamics");
    case AudioChainStage::Loudness:
        return QStringLiteral("Loudness");
    }
    return QStringLiteral("EQ");
}

} // namespace

AudioChainDialog::AudioChainDialog(const AudioChainOrder &current, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Audio chain"));
    setMinimumWidth(360);

    auto *layout = new QVBoxLayout(this);

    auto *hint = new QLabel(QStringLiteral("First item is applied first. Disabled effects stay in the list "
                                           "but are skipped while they are off."),
                            this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setMinimumHeight(140);
    layout->addWidget(m_list);

    auto *buttonRow = new QHBoxLayout();
    m_upButton = new QPushButton(QStringLiteral("Up"), this);
    m_downButton = new QPushButton(QStringLiteral("Down"), this);
    auto *resetButton = new QPushButton(QStringLiteral("Reset"), this);
    buttonRow->addWidget(m_upButton);
    buttonRow->addWidget(m_downButton);
    buttonRow->addStretch();
    buttonRow->addWidget(resetButton);
    layout->addLayout(buttonRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);

    populateList(normalizeAudioChainOrder(current));

    connect(m_upButton, &QPushButton::clicked, this, [this]() { moveSelection(-1); });
    connect(m_downButton, &QPushButton::clicked, this, [this]() { moveSelection(1); });
    connect(resetButton, &QPushButton::clicked, this, [this]() {
        populateList(defaultAudioChainOrder());
        emitCurrentOrder();
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { updateButtons(); });
    if (m_list->model()) {
        connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
            if (m_updating) {
                return;
            }
            emitCurrentOrder();
            updateButtons();
        });
    }
}

AudioChainOrder AudioChainDialog::order() const
{
    AudioChainOrder result = defaultAudioChainOrder();
    if (!m_list || m_list->count() != kAudioChainStageCount) {
        return result;
    }

    for (int i = 0; i < kAudioChainStageCount; ++i) {
        const QListWidgetItem *item = m_list->item(i);
        if (!item) {
            return defaultAudioChainOrder();
        }
        result.stages[static_cast<size_t>(i)] =
            static_cast<AudioChainStage>(item->data(Qt::UserRole).toInt());
    }
    return normalizeAudioChainOrder(result);
}

void AudioChainDialog::populateList(const AudioChainOrder &order)
{
    if (!m_list) {
        return;
    }

    const AudioChainOrder normalized = normalizeAudioChainOrder(order);
    m_updating = true;
    m_list->clear();
    for (AudioChainStage stage : normalized.stages) {
        auto *item = new QListWidgetItem(stageLabel(stage));
        item->setData(Qt::UserRole, static_cast<int>(stage));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_list->addItem(item);
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
    m_updating = false;
    updateButtons();
}

void AudioChainDialog::emitCurrentOrder()
{
    emit orderChanged(order());
}

void AudioChainDialog::moveSelection(int delta)
{
    if (!m_list || m_updating) {
        return;
    }

    const int row = m_list->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_list->count()) {
        return;
    }

    m_updating = true;
    QListWidgetItem *item = m_list->takeItem(row);
    m_list->insertItem(target, item);
    m_list->setCurrentRow(target);
    m_updating = false;
    emitCurrentOrder();
    updateButtons();
}

void AudioChainDialog::updateButtons()
{
    const int row = m_list ? m_list->currentRow() : -1;
    const int count = m_list ? m_list->count() : 0;
    if (m_upButton) {
        m_upButton->setEnabled(row > 0);
    }
    if (m_downButton) {
        m_downButton->setEnabled(row >= 0 && row < count - 1);
    }
}
