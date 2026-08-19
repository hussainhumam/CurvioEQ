#include "hrtfloader.h"

#include "resampler.h"
#include "surroundprocessor.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

struct WavInfo {
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    bool isFloat = false;
    qint64 dataOffset = 0;
    qint64 dataSize = 0;
};

bool readWavInfo(QFile &file, WavInfo *info, QString *errorMessage)
{
    if (!info) {
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open HRIR file: %1").arg(file.fileName());
        }
        return false;
    }

    char riffHeader[12]{};
    if (file.read(riffHeader, 12) != 12 || std::memcmp(riffHeader, "RIFF", 4) != 0
        || std::memcmp(riffHeader + 8, "WAVE", 4) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid WAV header: %1").arg(file.fileName());
        }
        return false;
    }

    bool foundFmt = false;
    bool foundData = false;
    while (!file.atEnd()) {
        char chunkId[4]{};
        quint32 chunkSize = 0;
        if (file.read(chunkId, 4) != 4 || file.read(reinterpret_cast<char *>(&chunkSize), 4) != 4) {
            break;
        }

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            quint16 formatTag = 0;
            quint16 channels = 0;
            quint32 sampleRate = 0;
            quint32 byteRate = 0;
            quint16 blockAlign = 0;
            quint16 bitsPerSample = 0;

            if (file.read(reinterpret_cast<char *>(&formatTag), 2) != 2
                || file.read(reinterpret_cast<char *>(&channels), 2) != 2
                || file.read(reinterpret_cast<char *>(&sampleRate), 4) != 4
                || file.read(reinterpret_cast<char *>(&byteRate), 4) != 4
                || file.read(reinterpret_cast<char *>(&blockAlign), 2) != 2
                || file.read(reinterpret_cast<char *>(&bitsPerSample), 2) != 2) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Invalid fmt chunk: %1").arg(file.fileName());
                }
                return false;
            }

            if (chunkSize > 16) {
                file.seek(file.pos() + static_cast<qint64>(chunkSize - 16));
            }

            info->sampleRate = static_cast<int>(sampleRate);
            info->channels = static_cast<int>(channels);
            info->bitsPerSample = static_cast<int>(bitsPerSample);
            info->isFloat = formatTag == 3;
            foundFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            info->dataOffset = file.pos();
            info->dataSize = static_cast<qint64>(chunkSize);
            file.seek(file.pos() + static_cast<qint64>(chunkSize));
            foundData = true;
        } else {
            file.seek(file.pos() + static_cast<qint64>(chunkSize));
        }
    }

    if (!foundFmt || !foundData) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Missing fmt/data chunk: %1").arg(file.fileName());
        }
        return false;
    }

    return true;
}

bool decodeWavMono(const QString &filePath, std::vector<float> *mono, int *sourceSampleRate, QString *errorMessage)
{
    if (!mono || !sourceSampleRate) {
        return false;
    }

    QFile file(filePath);
    WavInfo info;
    if (!readWavInfo(file, &info, errorMessage)) {
        return false;
    }

    if (info.bitsPerSample != 32 || !info.isFloat) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("HRIR WAV must be 32-bit float: %1").arg(filePath);
        }
        return false;
    }

    if (!file.seek(info.dataOffset)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not seek WAV data: %1").arg(filePath);
        }
        return false;
    }

    const int frameCount = static_cast<int>(info.dataSize / static_cast<qint64>(info.channels * sizeof(float)));
    std::vector<float> interleaved(static_cast<size_t>(frameCount * info.channels));
    if (file.read(reinterpret_cast<char *>(interleaved.data()),
                  static_cast<qint64>(interleaved.size() * sizeof(float)))
        != static_cast<qint64>(interleaved.size() * sizeof(float))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not read WAV samples: %1").arg(filePath);
        }
        return false;
    }

    mono->assign(static_cast<size_t>(frameCount), 0.f);
    for (int frame = 0; frame < frameCount; ++frame) {
        float sum = 0.f;
        for (int channel = 0; channel < info.channels; ++channel) {
            sum += interleaved[static_cast<size_t>(frame * info.channels + channel)];
        }
        (*mono)[static_cast<size_t>(frame)] = sum / static_cast<float>(info.channels);
    }

    *sourceSampleRate = info.sampleRate;
    return true;
}

bool resampleMono(const std::vector<float> &input, int inputRate, float outputRate, std::vector<float> *output)
{
    if (!output || input.empty() || inputRate <= 0 || outputRate <= 0.f) {
        return false;
    }

    if (std::fabs(static_cast<float>(inputRate) - outputRate) < 0.5f) {
        *output = input;
        return true;
    }

    Resampler resampler;
    resampler.configure(static_cast<float>(inputRate), outputRate, 1);
    const int estimated = resampler.estimateOutputFrames(static_cast<int>(input.size())) + 1;
    output->assign(static_cast<size_t>(estimated), 0.f);
    const int outFrames = resampler.process(input.data(), static_cast<int>(input.size()), output->data(), estimated);
    output->resize(static_cast<size_t>(std::max(0, outFrames)));
    return !output->empty();
}

const char *speakerFileStem(SurroundProcessor::Channel channel)
{
    switch (channel) {
    case SurroundProcessor::FrontLeft:
        return "FL";
    case SurroundProcessor::FrontRight:
        return "FR";
    case SurroundProcessor::FrontCenter:
        return "FC";
    case SurroundProcessor::Lfe:
        return "LFE";
    case SurroundProcessor::BackLeft:
        return "BL";
    case SurroundProcessor::BackRight:
        return "BR";
    case SurroundProcessor::SideLeft:
        return "SL";
    case SurroundProcessor::SideRight:
        return "SR";
    default:
        return "FL";
    }
}

void copyToFixedLength(const std::vector<float> &source, std::vector<float> *dest, int targetLength)
{
    dest->assign(static_cast<size_t>(targetLength), 0.f);
    const int copyCount = std::min(static_cast<int>(source.size()), targetLength);
    for (int i = 0; i < copyCount; ++i) {
        (*dest)[static_cast<size_t>(i)] = source[static_cast<size_t>(i)];
    }
}

} // namespace

QString HrtfLoader::presetDirectoryName(HrtfPresetId presetId)
{
    switch (presetId) {
    case HrtfPresetId::Wide:
        return QStringLiteral("wide");
    case HrtfPresetId::Close:
        return QStringLiteral("close");
    case HrtfPresetId::Default:
    default:
        return QStringLiteral("default");
    }
}

QString HrtfLoader::bundledHrtfRoot(const QString &applicationDirPath)
{
    return QDir(applicationDirPath).filePath(QStringLiteral("hrtf"));
}

bool HrtfLoader::loadPresetFromDirectory(const QString &directoryPath,
                                         HrtfPresetId presetId,
                                         float targetSampleRate,
                                         HrtfPresetData *outData,
                                         QString *errorMessage)
{
    if (!outData) {
        return false;
    }

    QDir dir(directoryPath);
    if (!dir.exists()) {
        return false;
    }

    outData->sampleRate = targetSampleRate;
    outData->irLength = HrtfPresets::kIrLength;

    for (int channel = 0; channel < SurroundProcessor::kChannelCount; ++channel) {
        const auto speaker = static_cast<SurroundProcessor::Channel>(channel);
        const QString leftPath = dir.filePath(QStringLiteral("%1_L.wav").arg(QString::fromLatin1(speakerFileStem(speaker))));
        const QString rightPath = dir.filePath(QStringLiteral("%1_R.wav").arg(QString::fromLatin1(speakerFileStem(speaker))));

        std::vector<float> leftMono;
        std::vector<float> rightMono;
        int leftRate = 0;
        int rightRate = 0;

        if (!decodeWavMono(leftPath, &leftMono, &leftRate, errorMessage)
            || !decodeWavMono(rightPath, &rightMono, &rightRate, errorMessage)) {
            return false;
        }

        std::vector<float> leftResampled;
        std::vector<float> rightResampled;
        if (!resampleMono(leftMono, leftRate, targetSampleRate, &leftResampled)
            || !resampleMono(rightMono, rightRate, targetSampleRate, &rightResampled)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not resample HRIR for %1").arg(leftPath);
            }
            return false;
        }

        copyToFixedLength(leftResampled, &outData->speakers[static_cast<size_t>(channel)].left, HrtfPresets::kIrLength);
        copyToFixedLength(rightResampled, &outData->speakers[static_cast<size_t>(channel)].right, HrtfPresets::kIrLength);
    }

    return true;
}
