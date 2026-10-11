/* Tests of the silence padding around a stream, driven by a fake clock and a scripted device. */
#include <functional>
#include <memory>
#include <vector>

#include "../src/engine/padded_source.h"
#include "test_support.h"

using namespace audiorgui;

namespace {

/* A device that hands over, at each poll, the frames the test has made available. */
class ScriptedDevice final : public ICaptureSource {
public:
    ScriptedDevice(std::size_t *available, HRESULT pollHr) : available_(available), pollHr_(pollHr) {}

    HRESULT Open() override { return S_OK; }
    HRESULT Start() override { return S_OK; }
    HRESULT Poll(std::vector<std::int16_t> *frames) override
    {
        frames->insert(frames->end(), *available_ * kPcmChannels, 500);
        *available_ = 0;
        return pollHr_;
    }
    void Stop() override {}

private:
    std::size_t *available_;
    HRESULT      pollHr_;
};

/* Polls a padded stream every 10 ms of fake time for `seconds`; `device(poll)` says how many frames
 * the device has by then.  Returns how much audio came out, in seconds. */
double Recorded(double seconds, const std::function<std::size_t(int)> &device)
{
    double now = 0.0;
    std::size_t available = 0;
    PaddedSource source(std::make_unique<ScriptedDevice>(&available, S_OK), [&] { return now; });
    source.Start();

    std::vector<std::int16_t> frames;
    std::size_t total = 0;
    for (int poll = 1; poll * 0.010 <= seconds + 1e-9; ++poll) {
        now = poll * 0.010;
        available = device(poll);
        frames.clear();
        source.Poll(&frames);
        total += frames.size() / kPcmChannels;
    }
    return static_cast<double>(total) / kPcmRate;
}

}  // namespace

TEST(PaddedSource_AnIdleDeviceGivesTheClocksLength)
{
    const auto nothing = [](int) -> std::size_t { return 0; };
    CHECK_NEAR(Recorded(32.0, nothing), 32.0, 0.05);
    CHECK_NEAR(Recorded(62.0, nothing), 62.0, 0.05);
}

TEST(PaddedSource_ADeviceThatDeliversInBurstsGivesTheClocksLength)
{
    const auto bursts = [](int poll) -> std::size_t { return poll % 3 == 0 ? 3 * 480 : 0; };    /* 30 ms at a time */
    CHECK_NEAR(Recorded(32.0, bursts), 32.0, 0.05);
    CHECK_NEAR(Recorded(62.0, bursts), 62.0, 0.05);
}

TEST(PaddedSource_ADeviceThatStopsPlayingKeepsTheLength)
{
    const auto playsForTenSeconds = [](int poll) -> std::size_t { return poll <= 1000 ? 480 : 0; };
    CHECK_NEAR(Recorded(32.0, playsForTenSeconds), 32.0, 0.05);
}

TEST(PaddedSource_BeginForgetsTheTimeBeforeIt)
{
    double now = 0.0;
    std::size_t available = 0;
    PaddedSource source(std::make_unique<ScriptedDevice>(&available, S_OK), [&] { return now; });
    source.Start();
    now = 0.7;                                          /* another device was slow to start */
    source.Begin();

    std::vector<std::int16_t> frames;
    source.Poll(&frames);
    CHECK(frames.empty());                              /* none of the 0.7 s is owed */
    now = 1.7;
    source.Poll(&frames);
    CHECK_NEAR(static_cast<double>(frames.size() / kPcmChannels) / kPcmRate, 1.0, 0.001);
}

TEST(PaddedSource_AFailureIsPassedOnWithoutPadding)
{
    double now = 0.0;
    std::size_t available = 480;
    PaddedSource source(std::make_unique<ScriptedDevice>(&available, static_cast<HRESULT>(0x88890004)), [&] { return now; });
    source.Start();
    now = 5.0;
    std::vector<std::int16_t> frames;
    CHECK_EQ(source.Poll(&frames), static_cast<HRESULT>(0x88890004));
    CHECK_EQ(frames.size(), std::size_t(480 * kPcmChannels));       /* what came before the failure only */
}
