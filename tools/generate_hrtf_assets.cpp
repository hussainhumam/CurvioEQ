#include "audio/hrtfpresets.h"
#include "audio/surroundprocessor.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void writeFloatWavMono(const std::filesystem::path &path, const std::vector<float> &samples, int sampleRate)
{
    std::filesystem::create_directories(path.parent_path());

    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(float));
    const uint32_t riffSize = 36 + dataBytes;
    const uint16_t channels = 1;
    const uint16_t bitsPerSample = 32;
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate * channels * bitsPerSample / 8);
    const uint16_t blockAlign = static_cast<uint16_t>(channels * bitsPerSample / 8);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char *>(&riffSize), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    const uint32_t fmtSize = 16;
    const uint16_t formatTag = 3;
    file.write(reinterpret_cast<const char *>(&fmtSize), 4);
    file.write(reinterpret_cast<const char *>(&formatTag), 2);
    file.write(reinterpret_cast<const char *>(&channels), 2);
    const uint32_t sampleRateU32 = static_cast<uint32_t>(sampleRate);
    file.write(reinterpret_cast<const char *>(&sampleRateU32), 4);
    file.write(reinterpret_cast<const char *>(&byteRate), 4);
    file.write(reinterpret_cast<const char *>(&blockAlign), 2);
    file.write(reinterpret_cast<const char *>(&bitsPerSample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<const char *>(&dataBytes), 4);
    file.write(reinterpret_cast<const char *>(samples.data()), static_cast<std::streamsize>(dataBytes));
}

const char *speakerStem(SurroundProcessor::Channel channel)
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

void writePreset(const std::filesystem::path &outputRoot, HrtfPresetId presetId)
{
    const HrtfPresetData preset = HrtfPresets::preset(presetId, 48000.f);
    std::string folderName;
    switch (presetId) {
    case HrtfPresetId::Wide:
        folderName = "wide";
        break;
    case HrtfPresetId::Close:
        folderName = "close";
        break;
    case HrtfPresetId::Default:
    default:
        folderName = "default";
        break;
    }

    const std::filesystem::path presetDir = outputRoot / folderName;
    for (int channel = 0; channel < SurroundProcessor::kChannelCount; ++channel) {
        const auto speaker = static_cast<SurroundProcessor::Channel>(channel);
        const HrtfSpeakerImpulse &impulse = preset.speakers[static_cast<size_t>(channel)];
        const std::string stem = speakerStem(speaker);
        writeFloatWavMono(presetDir / (stem + "_L.wav"), impulse.left, static_cast<int>(preset.sampleRate));
        writeFloatWavMono(presetDir / (stem + "_R.wav"), impulse.right, static_cast<int>(preset.sampleRate));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    std::filesystem::path outputRoot = std::filesystem::path("assets") / "hrtf";
    if (argc > 1) {
        outputRoot = std::filesystem::path(argv[1]);
    }

    writePreset(outputRoot, HrtfPresetId::Default);
    writePreset(outputRoot, HrtfPresetId::Wide);
    writePreset(outputRoot, HrtfPresetId::Close);
    return 0;
}
