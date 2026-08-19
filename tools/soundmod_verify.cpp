#include "soundmods/backupstore.h"
#include "soundmods/formathandlers.h"
#include "soundmods/patchengine.h"
#include "soundmods/wavautil.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

void expectTrue(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "  [FAIL] %s\n", message);
        ++g_failures;
    } else {
        std::fprintf(stdout, "  [OK]   %s\n", message);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString tempRoot = QDir::tempPath() + QStringLiteral("/curvioeq_soundmod_test");
    QDir(tempRoot).removeRecursively();
    QDir().mkpath(tempRoot);

    const QString sourcePath = QDir(tempRoot).filePath(QStringLiteral("tone.wav"));
    WavAudioUtil::AudioBuffer buffer;
    buffer.channels = 1;
    buffer.sampleRate = 48000;
    buffer.interleaved.resize(4800);
    for (size_t i = 0; i < buffer.interleaved.size(); ++i) {
        buffer.interleaved[i] = 0.2f * std::sin(2.f * 3.14159265358979323846f * static_cast<float>(i) / 480.f);
    }
    expectTrue(WavAudioUtil::save(sourcePath, buffer), "write test wav");

    LooseAudioHandler handler;
    const QString stagedPath = QDir(tempRoot).filePath(QStringLiteral("tone_patched.wav"));
    QString error;
    expectTrue(handler.applyGain(sourcePath, stagedPath, 6.f, &error), "apply +6 dB gain to wav");

    WavAudioUtil::AudioBuffer boosted;
    expectTrue(WavAudioUtil::load(stagedPath, &boosted, &error), "load patched wav");

    float sourceRms = 0.f;
    float boostedRms = 0.f;
    for (float sample : buffer.interleaved) {
        sourceRms += sample * sample;
    }
    for (float sample : boosted.interleaved) {
        boostedRms += sample * sample;
    }
    sourceRms = std::sqrt(sourceRms / static_cast<float>(buffer.interleaved.size()));
    boostedRms = std::sqrt(boostedRms / static_cast<float>(boosted.interleaved.size()));
    const float boostDb = 20.f * std::log10(boostedRms / std::max(sourceRms, 1e-6f));
    expectTrue(boostDb > 4.f, "patched wav rms increased");

    const QString gameId = QStringLiteral("test_game");
    BackupStore backupStore;
    expectTrue(backupStore.ensureBackup(gameId, sourcePath, QStringLiteral("tone.wav"), &error), "backup original");

    QFile::remove(sourcePath);
    expectTrue(QFile::copy(stagedPath, sourcePath), "replace source with patched");

    expectTrue(backupStore.restoreFile(gameId, QStringLiteral("tone.wav"), sourcePath, &error), "restore backup");
    WavAudioUtil::AudioBuffer restored;
    expectTrue(WavAudioUtil::load(sourcePath, &restored, &error), "load restored wav");

    float restoredRms = 0.f;
    for (float sample : restored.interleaved) {
        restoredRms += sample * sample;
    }
    restoredRms = std::sqrt(restoredRms / static_cast<float>(restored.interleaved.size()));
    expectTrue(std::fabs(restoredRms - sourceRms) < 0.02f, "restored wav matches original rms");

    GameIdentity game;
    game.id = gameId;
    game.displayName = QStringLiteral("Test Game");
    game.scanRoot = tempRoot;

    SoundAssetEntry asset;
    asset.absolutePath = sourcePath;
    asset.relativePath = QStringLiteral("tone.wav");
    asset.displayName = QStringLiteral("tone.wav");
    asset.format = SoundAssetFormat::Wav;
    asset.patchSupport = SoundPatchSupport::Full;
    asset.gainDb = 3.f;
    asset.enabled = true;
    asset.status = SoundAssetStatus::Pending;

    PatchApplyResult applyResult = PatchEngine().apply(game, {asset}, tempRoot);
    expectTrue(applyResult.patchedCount == 1, "patch engine applied one file");

    asset.gainDb = 0.f;
    PatchApplyResult restoreResult = PatchEngine().restoreAll(game, {asset}, tempRoot);
    expectTrue(restoreResult.restoredCount >= 1, "patch engine restored files");

    QDir(tempRoot).removeRecursively();

    if (g_failures == 0) {
        std::fprintf(stdout, "Sound mod tests: HEALTHY\n");
        return 0;
    }

    std::fprintf(stderr, "Sound mod tests: UNHEALTHY (%d failed)\n", g_failures);
    return 1;
}
