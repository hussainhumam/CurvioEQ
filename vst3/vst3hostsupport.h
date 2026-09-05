#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <QByteArray>
#include <QString>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

namespace CurvioVst3 {

Steinberg::Vst::IHostApplication *hostApplication();

class MemoryStream : public Steinberg::IBStream
{
public:
    MemoryStream() = default;
    explicit MemoryStream(QByteArray data);

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;
    Steinberg::tresult PLUGIN_API read(void *buffer, Steinberg::int32 numBytes,
                                       Steinberg::int32 *numBytesRead) override;
    Steinberg::tresult PLUGIN_API write(void *buffer, Steinberg::int32 numBytes,
                                        Steinberg::int32 *numBytesWritten) override;
    Steinberg::tresult PLUGIN_API seek(Steinberg::int64 pos, Steinberg::int32 mode,
                                       Steinberg::int64 *result) override;
    Steinberg::tresult PLUGIN_API tell(Steinberg::int64 *pos) override;

    QByteArray data() const { return m_data; }

private:
    std::atomic<Steinberg::uint32> m_ref{1};
    QByteArray m_data;
    Steinberg::int32 m_pos = 0;
};

struct PendingEdit {
    Steinberg::Vst::ParamID id = 0;
    Steinberg::Vst::ParamValue value = 0;
};

class ComponentHandler : public Steinberg::Vst::IComponentHandler
{
public:
    std::mutex mutex;
    std::vector<PendingEdit> edits;

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue valueNormalized) override;
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;
};

class ParamValueQueue : public Steinberg::Vst::IParamValueQueue
{
public:
    Steinberg::Vst::ParamID id = 0;
    Steinberg::Vst::ParamValue value = 0;

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override;
    Steinberg::int32 PLUGIN_API getPointCount() override;
    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index, Steinberg::int32 &sampleOffset,
                                           Steinberg::Vst::ParamValue &valueOut) override;
    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::int32 &index) override;
};

constexpr int kMaxParamChanges = 32;

class ParameterChanges : public Steinberg::Vst::IParameterChanges
{
public:
    std::array<ParamValueQueue, kMaxParamChanges> queues{};
    Steinberg::int32 count = 0;

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;
    Steinberg::int32 PLUGIN_API getParameterCount() override;
    Steinberg::Vst::IParamValueQueue *PLUGIN_API getParameterData(Steinberg::int32 index) override;
    Steinberg::Vst::IParamValueQueue *PLUGIN_API addParameterData(const Steinberg::Vst::ParamID &id,
                                                                  Steinberg::int32 &index) override;
};

QString tuidToString(const Steinberg::TUID tuid);
bool tuidFromString(const QString &text, Steinberg::TUID tuid);
void copyToString128(const QString &text, Steinberg::Vst::String128 out);
QString fromString128(const Steinberg::Vst::String128 text);
QString moduleBinaryPath(const QString &path);

} // namespace CurvioVst3
