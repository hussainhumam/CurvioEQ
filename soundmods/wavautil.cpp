#include "wavautil.h"

#include <QCryptographicHash>
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

    char riffHeader[12]{};
    if (file.read(riffHeader, 12) != 12 || std::memcmp(riffHeader, "RIFF", 4) != 0
        || std::memcmp(riffHeader + 8, "WAVE", 4) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid WAV header");
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
            quint16 bitsPerSample = 0;
            quint32 byteRate = 0;
            quint16 blockAlign = 0;
            if (file.read(reinterpret_cast<char *>(&formatTag), 2) != 2
                || file.read(reinterpret_cast<char *>(&channels), 2) != 2
                || file.read(reinterpret_cast<char *>(&sampleRate), 4) != 4
                || file.read(reinterpret_cast<char *>(&byteRate), 4) != 4
                || file.read(reinterpret_cast<char *>(&blockAlign), 2) != 2
                || file.read(reinterpret_cast<char *>(&bitsPerSample), 2) != 2) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Invalid fmt chunk");
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
            *errorMessage = QStringLiteral("Missing fmt/data chunk");
        }
        return false;
    }
    return true;
}

} // namespace

bool WavAudioUtil::load(const QString &path, AudioBuffer *output, QString *errorMessage)
{
    if (!output) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open WAV file");
        }
        return false;
    }

    WavInfo info;
    if (!readWavInfo(file, &info, errorMessage)) {
        return false;
    }

    if (info.channels <= 0 || info.sampleRate <= 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid WAV channel/sample rate");
        }
        return false;
    }

    if (!file.seek(info.dataOffset)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not seek WAV data");
        }
        return false;
    }

    const int bytesPerSample = info.bitsPerSample / 8;
    const int frameCount =
        static_cast<int>(info.dataSize / static_cast<qint64>(info.channels * bytesPerSample));
    output->channels = info.channels;
    output->sampleRate = info.sampleRate;
    output->interleaved.assign(static_cast<size_t>(frameCount * info.channels), 0.f);

    if (info.isFloat && info.bitsPerSample == 32) {
        const qint64 bytesToRead = static_cast<qint64>(output->interleaved.size() * sizeof(float));
        if (file.read(reinterpret_cast<char *>(output->interleaved.data()), bytesToRead) != bytesToRead) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not read float WAV samples");
            }
            return false;
        }
        return true;
    }

    if (info.bitsPerSample == 16) {
        std::vector<int16_t> pcm(static_cast<size_t>(frameCount * info.channels));
        const qint64 bytesToRead = static_cast<qint64>(pcm.size() * sizeof(int16_t));
        if (file.read(reinterpret_cast<char *>(pcm.data()), bytesToRead) != bytesToRead) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not read PCM WAV samples");
            }
            return false;
        }
        for (size_t i = 0; i < pcm.size(); ++i) {
            output->interleaved[i] = static_cast<float>(pcm[i]) / 32768.f;
        }
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Unsupported WAV bit depth");
    }
    return false;
}

bool WavAudioUtil::save(const QString &path, const AudioBuffer &input, QString *errorMessage)
{
    if (input.channels <= 0 || input.sampleRate <= 0 || input.interleaved.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Empty audio buffer");
        }
        return false;
    }

    const int frameCount = static_cast<int>(input.interleaved.size()) / input.channels;
    const uint32_t dataBytes = static_cast<uint32_t>(input.interleaved.size() * sizeof(float));
    const uint32_t riffSize = 36 + dataBytes;
    const uint16_t channels = static_cast<uint16_t>(input.channels);
    const uint16_t bitsPerSample = 32;
    const uint32_t byteRate = static_cast<uint32_t>(input.sampleRate * channels * bitsPerSample / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(channels * bitsPerSample / 8);
    const uint32_t fmtSize = 16;
    const uint16_t formatTag = 3;
    const uint32_t sampleRateU32 = static_cast<uint32_t>(input.sampleRate);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write WAV file");
        }
        return false;
    }

    auto writeBytes = [&file](const void *data, int size) {
        return file.write(static_cast<const char *>(data), size) == size;
    };

    if (!writeBytes("RIFF", 4) || !writeBytes(&riffSize, 4) || !writeBytes("WAVE", 4) || !writeBytes("fmt ", 4)
        || !writeBytes(&fmtSize, 4) || !writeBytes(&formatTag, 2) || !writeBytes(&channels, 2)
        || !writeBytes(&sampleRateU32, 4) || !writeBytes(&byteRate, 4) || !writeBytes(&blockAlign, 2)
        || !writeBytes(&bitsPerSample, 2) || !writeBytes("data", 4) || !writeBytes(&dataBytes, 4)
        || file.write(reinterpret_cast<const char *>(input.interleaved.data()),
                      static_cast<qint64>(dataBytes))
               != static_cast<qint64>(dataBytes)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed writing WAV data");
        }
        return false;
    }

    return true;
}

void WavAudioUtil::applyGainDb(WavAudioUtil::AudioBuffer *buffer, float gainDb)
{
    if (!buffer) {
        return;
    }
    const float gain = std::pow(10.f, gainDb / 20.f);
    for (float &sample : buffer->interleaved) {
        sample = std::clamp(sample * gain, -1.f, 1.f);
    }
}

QString WavAudioUtil::sha256File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex());
}
