#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <array>
#include <memory>
#include <mutex>
#include <vector>

struct Vst3PluginInfo {
    QString name;
    QString vendor;
    QString uid;
    QString path;
    QString subCategories;
    bool hasAudioInput = true;
};

class Vst3Plugin
{
public:
    Vst3Plugin();
    ~Vst3Plugin();

    Vst3Plugin(const Vst3Plugin &) = delete;
    Vst3Plugin &operator=(const Vst3Plugin &) = delete;

    bool load(const QString &modulePath, const QString &uid, QString *errorMessage);
    void unload();

    bool isLoaded() const;
    QString name() const { return m_name; }
    QString vendor() const { return m_vendor; }
    QString uid() const { return m_uid; }
    QString path() const { return m_path; }
    bool hasAudioInput() const { return m_hasAudioInput; }

    void setSampleRate(float sampleRate, int maxBlockSize);
    bool process(float *interleaved, int frameCount, int channelCount);

    QByteArray saveState() const;
    bool restoreState(const QByteArray &state);

    bool openEditor(QWidget *parent);
    void closeEditor();
    bool editorOpen() const;

    static QVector<Vst3PluginInfo> scanFile(const QString &path);
    static QVector<Vst3PluginInfo> scanFolders(const QStringList &folders);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QString m_name;
    QString m_vendor;
    QString m_uid;
    QString m_path;
    bool m_hasAudioInput = true;
};
