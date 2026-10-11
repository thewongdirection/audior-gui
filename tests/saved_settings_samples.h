/*
 * saved_settings_samples.h - Settings for the tests of what is remembered between runs: a set with every
 * field away from its default, and the check that two sets are the same, field by field.
 */
#pragma once

#include "../src/core/asr_models.h"
#include "../src/core/saved_settings.h"
#include "test_support.h"

/* No field has the value a fresh SavedSettings gives it.  The folder and the name hold characters outside
 * ASCII, which the file must carry too. */
inline audiorgui::SavedSettings UnusualSettings()
{
    using namespace audiorgui;
    SavedSettings s;
    RecordingSettings &r = s.recording;
    r.outputDirectory = L"D:\\Aufnahmen\\M\u00FCzik \u9332\u97F3";
    r.fileName = L"weekly sync \u00E9.wav";
    r.format = Format::Wav;
    r.mp3BitrateKbps = 256;
    r.includeMicrophone = false;
    r.microphoneId = L"{0.0.1.00000000}.{aaaa1111}";
    r.micAmplifyPercent = 150;
    r.includeSystemAudio = true;
    r.systemDeviceId = L"{0.0.0.00000000}.{bbbb2222}";
    r.systemAmplifyPercent = 300;
    r.mixMode = MixMode::Separate;
    r.mergeMicPercent = 40;
    r.transcribeMicrophone = true;
    r.transcribeSystem = true;
    r.transcribeModel = kAsrModels[kAsrModelCount - 1].file;
    r.transcribeLanguage = SpeechLanguage::English;
    r.transcribeGpu = false;
    s.hotkeys[HotkeyIndex(HotkeyAction::Start)] = {kModAlt | kModShift, 'M'};
    s.hotkeys[HotkeyIndex(HotkeyAction::Stop)] = {0, 0x7A};                         /* F11 */
    s.downloadDeclined = true;
    return s;
}

inline void ExpectSame(const audiorgui::SavedSettings &a, const audiorgui::SavedSettings &b)
{
    using namespace audiorgui;
    const RecordingSettings &x = a.recording, &y = b.recording;
    CHECK_EQ(x.outputDirectory, y.outputDirectory);
    CHECK_EQ(x.fileName, y.fileName);
    CHECK(x.format == y.format);
    CHECK_EQ(x.mp3BitrateKbps, y.mp3BitrateKbps);
    CHECK_EQ(x.includeMicrophone, y.includeMicrophone);
    CHECK_EQ(x.microphoneId, y.microphoneId);
    CHECK_EQ(x.micAmplifyPercent, y.micAmplifyPercent);
    CHECK_EQ(x.includeSystemAudio, y.includeSystemAudio);
    CHECK_EQ(x.systemDeviceId, y.systemDeviceId);
    CHECK_EQ(x.systemAmplifyPercent, y.systemAmplifyPercent);
    CHECK(x.mixMode == y.mixMode);
    CHECK_EQ(x.mergeMicPercent, y.mergeMicPercent);
    CHECK_EQ(x.transcribeMicrophone, y.transcribeMicrophone);
    CHECK_EQ(x.transcribeSystem, y.transcribeSystem);
    CHECK_EQ(x.transcribeModel, y.transcribeModel);
    CHECK(x.transcribeLanguage == y.transcribeLanguage);
    CHECK_EQ(x.transcribeGpu, y.transcribeGpu);
    for (int i = 0; i < kHotkeyActionCount; ++i)
        CHECK(a.hotkeys[i] == b.hotkeys[i]);
    CHECK_EQ(a.downloadDeclined, b.downloadDeclined);
}
