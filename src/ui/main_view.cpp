#include "main_view.h"

#include <algorithm>
#include <cmath>

#include "../core/bluetooth.h"
#include "../core/format.h"

namespace audiorgui::ui {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kLinkWidth = 170.0f;        /* room for "Record system audio only" at the small size */

const LaneStyle kMicStyle    = {L"Microphone",  theme::kMic};
const LaneStyle kSystemStyle = {L"System audio", theme::kSystem};

std::wstring GainText(int percent) { return L"+" + std::to_wstring(percent) + L"%"; }

std::wstring ShareText(int micPercent)
{
    return L"Mic " + std::to_wstring(micPercent) + L"%  ·  System " + std::to_wstring(100 - micPercent) + L"%";
}

const wchar_t kHotkeyIdleHint[]   = L"Work in any program. Click one to change it.";
const wchar_t kHotkeyListenHint[] = L"Press the new shortcut. Esc cancels.";

std::wstring FileName(const std::wstring &path)
{
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

/* The end of a long path, which is the part that tells folders apart. */
std::wstring Tail(const std::wstring &path, std::size_t keep)
{
    return path.size() <= keep ? path : L"…" + path.substr(path.size() - keep);
}

}  // namespace

/* ====================================================================== */
/* Construction: widgets and what they do                                  */
/* ====================================================================== */

MainView::MainView(RecorderController &controller, IViewHost *host)
    : ctl_(controller), host_(host), fileNameBox_(IsFileNameChar, kMaxOutputPathChars)
{
    micCheck_.accent = theme::kMic;
    micCheck_.onChange = [this](bool on) {
        ctl_.Settings().includeMicrophone = on;
        Refresh();
    };
    systemCheck_.accent = theme::kSystem;
    systemCheck_.onChange = [this](bool on) {
        ctl_.Settings().includeSystemAudio = on;
        Refresh();
    };

    micTranscript_.text = systemTranscript_.text = L"Transcript";
    micTranscript_.accent = theme::kMic;
    micTranscript_.onChange = [this](bool on) {
        ctl_.Settings().transcribeMicrophone = on;
        Refresh();
    };
    systemTranscript_.accent = theme::kSystem;
    systemTranscript_.onChange = [this](bool on) {
        ctl_.Settings().transcribeSystem = on;
        Refresh();
    };

    micField_.style = Button::Style::Field;
    micField_.onClick = [this] {
        ChooseDevice(DeviceKind::Microphone, &RecordingSettings::microphoneId, micField_);
    };
    systemField_.style = Button::Style::Field;
    systemField_.onClick = [this] {
        ChooseDevice(DeviceKind::Playback, &RecordingSettings::systemDeviceId, systemField_);
    };

    ConfigureGain(StreamId::Microphone, &RecordingSettings::micAmplifyPercent, theme::kMic);
    ConfigureGain(StreamId::System, &RecordingSettings::systemAmplifyPercent, theme::kSystem);

    folderField_.style = Button::Style::Field;
    folderField_.onClick = [this] {
        if (!host_)
            return;
        const std::wstring dir = host_->PickFolder(ctl_.Settings().outputDirectory);
        if (!dir.empty()) {
            ctl_.Settings().outputDirectory = dir;
            Refresh();
        }
    };

    fileNameBox_.placeholder = L"Automatic (date and time)";
    fileNameBox_.onChange = [this](const std::wstring &name) {
        ctl_.Settings().fileName = name;
        Refresh();
    };
    fileNameBox_.clipboardGet = [this] { return host_ ? host_->ClipboardText() : std::wstring(); };
    fileNameBox_.clipboardSet = [this](const std::wstring &text) {
        if (host_)
            host_->SetClipboardText(text);
    };

    formatSeg_.options = {L"MP3", L"WAV"};
    formatSeg_.onChange = [this](int i) {
        ctl_.Settings().format = i == 0 ? Format::Mp3 : Format::Wav;
        Refresh();
    };

    bitrateField_.style = Button::Style::Field;
    bitrateField_.onClick = [this] { ChooseBitrate(); };

    mixSeg_.options = {L"One merged file", L"Two separate tracks"};
    mixSeg_.onChange = [this](int i) {
        ctl_.Settings().mixMode = i == 0 ? MixMode::Merge : MixMode::Separate;
        Refresh();
    };

    shareSlider_.minimum = 0;
    shareSlider_.maximum = 100;
    shareSlider_.step = 5;
    shareSlider_.accent = theme::kSystem;
    shareSlider_.onChange = [this](int v) {
        ctl_.Settings().mergeMicPercent = v;
        shareText_ = ShareText(v);
    };

    startButton_.text = L"Start recording";
    startButton_.style = Button::Style::Primary;
    startButton_.glyph = Button::Glyph::Record;
    startButton_.onClick = [this] { StartRecording(); };

    stopButton_.text = L"Stop";
    stopButton_.glyph = Button::Glyph::Stop;
    stopButton_.onClick = [this] { StopRecording(); };

    bluetoothLink_.style = Button::Style::Link;
    bluetoothLink_.text = L"Record system audio only";
    bluetoothLink_.onClick = [this] {                   /* the way round the headset's call mode */
        UseSystemAudioOnly(&ctl_.Settings());
        Refresh();
    };

    showButton_.text = L"Show in folder";
    showButton_.onClick = [this] {
        const Outcome &o = ctl_.LastOutcome();
        if (host_ && !o.files.empty())
            host_->ShowInFolder(o.files[0]);
    };

    ConfigureHotkey(HotkeyAction::Start, L"Start");
    ConfigureHotkey(HotkeyAction::Stop, L"Stop");

    Widget *const order[kWidgetCount] = {
        &micCheck_, &micTranscript_, &micField_, &bluetoothLink_, &gain_[0].slider, &systemCheck_,
        &systemTranscript_, &systemField_, &gain_[1].slider, &folderField_, &fileNameBox_, &formatSeg_,
        &bitrateField_, &mixSeg_, &shareSlider_, &keyBox_[0], &keyBox_[1], &startButton_, &stopButton_,
        &showButton_};
    std::copy(order, order + kWidgetCount, all_);

    RefreshHotkeys();
    Refresh();
}

/* The shortcut box of one action, wired to the rules that decide what it may become. */
void MainView::ConfigureHotkey(HotkeyAction action, const wchar_t *label)
{
    KeyBox &box = keyBox_[HotkeyIndex(action)];
    box.label = label;
    box.onBegin = [this] {                              /* a fresh try: forget the last complaint */
        hotkeyProblem_.clear();
        RefreshHotkeys();
    };
    box.onCapture = [this, action](const Hotkey &hotkey) { return ChangeHotkey(action, hotkey); };
}

/* The gain slider of one stream, wired to the setting it edits. */
void MainView::ConfigureGain(StreamId stream, int RecordingSettings::*percent, const D2D1_COLOR_F &accent)
{
    GainControl &gain = gain_[StreamIndex(stream)];
    gain.slider.minimum = 0;
    gain.slider.maximum = kMaxAmplifyPercent;
    gain.slider.step = 5;
    gain.slider.accent = accent;
    gain.slider.onChange = [this, &gain, percent](int v) {  /* only its own label: it fires on every drag step */
        ctl_.Settings().*percent = v;
        gain.text = GainText(v);
    };
}

/* ====================================================================== */
/* State -> widgets and text                                               */
/* ====================================================================== */

std::wstring MainView::DeviceName(DeviceKind kind, const std::wstring &id) const
{
    for (const DeviceInfo &d : ctl_.Devices(kind)) {
        if (id.empty() ? d.isDefault : d.id == id)
            return d.name;
    }
    return id.empty() ? L"No device found" : L"Device not available";
}

/* What a device drop-down shows: the device, or the system default and which
 * device that currently is. */
std::wstring MainView::DeviceFieldText(DeviceKind kind, const std::wstring &id) const
{
    return id.empty() ? L"Default · " + DeviceName(kind, id) : DeviceName(kind, id);
}

std::wstring MainView::Summarize(const RecordingSettings &s) const
{
    std::wstring t;
    if (RecordsBoth(s))
        t = L"Microphone + system audio";
    else if (s.includeMicrophone)
        t = L"Microphone";
    else if (s.includeSystemAudio)
        t = L"System audio";
    else
        return L"No stream selected";

    t += L"  ·  ";
    t += s.format == Format::Mp3 ? L"MP3 " + std::to_wstring(s.mp3BitrateKbps) + L" kbps"
                                 : std::wstring(L"WAV 48 kHz");
    if (RecordsBoth(s))
        t += s.mixMode == MixMode::Merge ? L"  ·  one merged file" : L"  ·  two separate tracks";
    return t;
}

void MainView::Refresh()
{
    const RecordingSettings &s = ctl_.Settings();
    const bool unlocked = !ctl_.SettingsLocked();
    const bool both = RecordsBoth(s);
    const bool merging = s.mixMode == MixMode::Merge;

    const bool canTranscribe = ctl_.CanTranscribe();
    micCheck_.checked = s.includeMicrophone;
    micCheck_.enabled = unlocked;
    micTranscript_.checked = s.transcribeMicrophone;
    micTranscript_.enabled = unlocked;
    micTranscript_.visible = canTranscribe && s.includeMicrophone;
    micField_.text = DeviceFieldText(DeviceKind::Microphone, s.microphoneId);
    micField_.enabled = unlocked && s.includeMicrophone;
    micBluetooth_ = s.includeMicrophone && MicrophoneIsBluetooth(ctl_.Devices(DeviceKind::Microphone), s.microphoneId);
    bluetoothLink_.visible = micBluetooth_ && unlocked;     /* not while recording: the settings are locked */
    gain_[0].slider.value = s.micAmplifyPercent;
    gain_[0].slider.enabled = micField_.enabled;
    gain_[0].text = GainText(s.micAmplifyPercent);

    systemCheck_.checked = s.includeSystemAudio;
    systemCheck_.enabled = unlocked;
    systemTranscript_.checked = s.transcribeSystem;
    systemTranscript_.enabled = unlocked;
    systemTranscript_.visible = canTranscribe && s.includeSystemAudio;
    systemField_.text = DeviceFieldText(DeviceKind::Playback, s.systemDeviceId);
    systemField_.enabled = unlocked && s.includeSystemAudio;
    gain_[1].slider.value = s.systemAmplifyPercent;
    gain_[1].slider.enabled = systemField_.enabled;
    gain_[1].text = GainText(s.systemAmplifyPercent);

    folderField_.text = s.outputDirectory.empty() ? L"Choose a folder…" : Tail(s.outputDirectory, 34);
    folderField_.enabled = unlocked;

    fileNameBox_.SetText(s.fileName);
    fileNameBox_.enabled = unlocked;
    const std::wstring problem = FileNameProblem(s.fileName);
    fileNameBox_.invalid = !problem.empty();
    nameHintIsError_ = !problem.empty();
    if (nameHintIsError_) {
        nameHint_ = problem;
    } else if (s.fileName.empty()) {
        nameHint_ = L"Empty: named after the date and time";
    } else {
        nameHint_ = L"Saved as ";
        const std::vector<std::wstring> names = PlannedOutputFiles(s, EnforceExtension(s.fileName, s.format));
        for (std::size_t i = 0; i < names.size(); ++i)
            nameHint_ += (i ? L" + " : L"") + names[i];
    }
    formatSeg_.selected = s.format == Format::Mp3 ? 0 : 1;
    formatSeg_.enabled = unlocked;
    bitrateField_.text = std::to_wstring(s.mp3BitrateKbps) + L" kbps";
    bitrateField_.enabled = unlocked && s.format == Format::Mp3;

    mixSeg_.selected = merging ? 0 : 1;
    mixSeg_.enabled = unlocked && both;
    mixHint_ = !both    ? L"Select both streams to merge or split them"
               : merging ? L"Both streams are mixed into one file"
                         : L"A _mic file and a _system file";
    shareSlider_.visible = both && merging;
    shareSlider_.enabled = unlocked;
    shareSlider_.value = s.mergeMicPercent;
    shareText_ = ShareText(s.mergeMicPercent);

    startButton_.enabled = ctl_.CanStart() && (s.includeMicrophone || s.includeSystemAudio);
    stopButton_.enabled = ctl_.CanStop();
    stopButton_.style = ctl_.CanStop() ? Button::Style::Prominent : Button::Style::Secondary;

    const Outcome &o = ctl_.LastOutcome();
    const bool kept = o.kind != Outcome::Kind::None && !o.files.empty() && ctl_.CanStart();
    showButton_.visible = kept;          /* after a failure too: the captured audio is kept */
    savedName_.clear();
    if (kept && o.kind == Outcome::Kind::Saved) {
        savedName_ = FileName(o.files[0]);
        if (o.files.size() > 1)
            savedName_ += L"  +" + std::to_wstring(o.files.size() - 1) + L" more";
    }

    summary_ = Summarize(ctl_.Active());

    /* Start and Stop swap roles: keep the keyboard focus on the live one. */
    if (focus_ && !focus_->Interactive()) {
        if (focus_ == &startButton_)
            focus_ = &stopButton_;
        else if (focus_ == &stopButton_)
            focus_ = &startButton_;
        else
            focus_ = nullptr;
    }
    lastPhase_ = ctl_.GetPhase();
}

void MainView::Update()
{
    if (ctl_.GetPhase() != lastPhase_)
        Refresh();          /* a recording ended by itself (a device failed) */
}

void MainView::ChooseDevice(DeviceKind kind, std::wstring RecordingSettings::*idField, const Widget &anchor)
{
    ctl_.RefreshDevices();                  /* pick up anything plugged in since the last look */
    const std::vector<DeviceInfo> &devices = ctl_.Devices(kind);
    std::vector<std::wstring> items{DeviceFieldText(kind, std::wstring())};
    int selected = 0;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        items.push_back(devices[i].name + (devices[i].isDefault ? L"  (default)" : L""));
        if (devices[i].id == ctl_.Settings().*idField)
            selected = static_cast<int>(i) + 1;
    }
    const int pick = host_ ? host_->Choose(items, selected, anchor.rect) : -1;
    if (pick < 0 || static_cast<std::size_t>(pick) >= items.size())
        return;
    ctl_.Settings().*idField = pick == 0 ? std::wstring() : devices[static_cast<std::size_t>(pick) - 1].id;
    Refresh();
}

void MainView::ChooseBitrate()
{
    std::vector<std::wstring> items;
    int selected = 0;
    for (int i = 0; i < kMp3BitrateCount; ++i) {
        items.push_back(std::to_wstring(kMp3Bitrates[i]) + L" kbps");
        if (kMp3Bitrates[i] == ctl_.Settings().mp3BitrateKbps)
            selected = i;
    }
    const int pick = host_ ? host_->Choose(items, selected, bitrateField_.rect) : -1;
    if (pick < 0 || pick >= kMp3BitrateCount)
        return;
    ctl_.Settings().mp3BitrateKbps = kMp3Bitrates[pick];
    Refresh();
}

void MainView::StartRecording()
{
    if (!CanStart())
        return;
    ctl_.Start();
    if (host_)
        host_->Wake();
    Refresh();
}

void MainView::StopRecording()
{
    ctl_.Stop();
    Refresh();
}

/* ====================================================================== */
/* Hotkeys                                                                 */
/* ====================================================================== */

/* The boxes' text, their outline and the line of advice under them. */
void MainView::RefreshHotkeys()
{
    hotkeyHint_ = hotkeyProblem_;
    for (int i = 0; i < kHotkeyActionCount; ++i) {
        keyBox_[i].text = FormatHotkey(hotkeys_[i]);
        keyBox_[i].invalid = !hotkeyActive_[i];
        if (hotkeyHint_.empty() && !hotkeyActive_[i])
            hotkeyHint_ = keyBox_[i].text + L" is in use by another program: it is off.";
    }
    hotkeyHintIsError_ = !hotkeyHint_.empty();
    if (!hotkeyHintIsError_)
        hotkeyHint_ = kHotkeyIdleHint;
}

/* A shortcut was typed for `action`.  True if the box is done with it (taken, or
 * refused for a reason that typing again will not fix); false to keep listening. */
bool MainView::ChangeHotkey(HotkeyAction action, const Hotkey &candidate)
{
    const int i = HotkeyIndex(action), other = kHotkeyActionCount - 1 - i;
    hotkeyProblem_ = HotkeyProblem(action, candidate, hotkeys_[other]);
    if (!hotkeyProblem_.empty()) {
        RefreshHotkeys();
        return false;
    }

    if (candidate != hotkeys_[i] || !hotkeyActive_[i]) {        /* an unchanged one that works needs no new registration */
        if (host_ && !host_->SetHotkey(action, candidate)) {
            hotkeyProblem_ = FormatHotkey(candidate) + L" is in use by another program.";
            RefreshHotkeys();
            return true;
        }
        hotkeys_[i] = candidate;
        hotkeyActive_[i] = true;
    }
    RefreshHotkeys();
    return true;
}

void MainView::SetHotkey(HotkeyAction action, const Hotkey &hotkey, bool registered)
{
    hotkeys_[HotkeyIndex(action)] = hotkey;
    hotkeyActive_[HotkeyIndex(action)] = registered;
    RefreshHotkeys();
}

bool MainView::ListeningForHotkey() const
{
    for (const KeyBox &box : keyBox_) {
        if (box.Capturing())
            return true;
    }
    return false;
}

void MainView::OfferHotkey(const Hotkey &hotkey)
{
    for (KeyBox &box : keyBox_)
        box.Capture(hotkey);
}

void MainView::ListenForHotkey(HotkeyAction action)
{
    KeyBox &box = keyBox_[HotkeyIndex(action)];
    FocusOn(&box);
    box.Key(VK_RETURN);
}

/* ====================================================================== */
/* Layout                                                                  */
/* ====================================================================== */

/* A pane's include checkbox, transcript switch and device drop-down. */
void MainView::LayoutPane(const D2D1_RECT_F &card, Checkbox &include, Switch &transcript, Button &device)
{
    include.rect = RectXYWH(card.left + 16, card.top + 10, 28, 28);
    transcript.rect = RectXYWH(card.left + LaneView::kSwitchLeft, card.top + 11, LaneView::kSwitchWidth,
                               LaneView::kSwitchHeight);
    device.rect = RectXYWH(card.left + 20, card.top + 44, std::min(320.0f, Width(card) * 0.5f - 20.0f), 36);
}

void MainView::Layout(float w, float h)
{
    using namespace theme;
    width_ = w;

    const float bottom = h - kTransportH - kGap;
    const D2D1_RECT_F content = RectF(kMargin, kHeaderH, w - kMargin - kPanelW - kGap, bottom);
    panel_     = RectF(w - kMargin - kPanelW, kHeaderH, w - kMargin, bottom);
    transport_ = RectF(0, h - kTransportH, w, h);

    /* Two equal panes, each with its own controls. */
    const float each = (Height(content) - kGap) * 0.5f;
    micCard_    = RectF(content.left, content.top, content.right, content.top + each);
    systemCard_ = RectF(content.left, content.bottom - each, content.right, content.bottom);
    LayoutPane(micCard_, micCheck_, micTranscript_, micField_);
    LayoutPane(systemCard_, systemCheck_, systemTranscript_, systemField_);
    micNoteRect_ = RectF(micField_.rect.left, micField_.rect.bottom + 2, micCard_.right - 20, micField_.rect.bottom + 20);
    bluetoothLink_.rect = RectF(micNoteRect_.right - kLinkWidth, micNoteRect_.top, micNoteRect_.right, micNoteRect_.bottom);
    const Button *const fields[kStreamCount] = {&micField_, &systemField_};
    const D2D1_RECT_F *const cards[kStreamCount] = {&micCard_, &systemCard_};
    for (int i = 0; i < kStreamCount; ++i) {            /* the gain sits right of the device drop-down */
        GainControl &gain = gain_[i];
        gain.row = RectF(fields[i]->rect.right + 20, cards[i]->top + 44, cards[i]->right - 20, cards[i]->top + 80);
        gain.slider.rect = RectF(gain.row.left + 40, gain.row.top + 6, gain.row.right - 52, gain.row.bottom - 6);
    }

    /* The settings panel, top to bottom. */
    const float pad = 18.0f, x = panel_.left + pad, iw = kPanelW - 2 * pad;
    float y = panel_.top + pad;
    captions_[0] = {L"SAVE TO", RectXYWH(x, y, iw, 14)};            y += 18;
    folderField_.rect = RectXYWH(x, y, iw, 36);                     y += 36 + 16;
    captions_[1] = {L"FILE NAME", RectXYWH(x, y, iw, 14)};          y += 18;
    fileNameBox_.rect = RectXYWH(x, y, iw, 36);                     y += 36 + 4;
    nameHintRect_ = RectXYWH(x, y, iw, 18);                         y += 18 + 14;
    captions_[2] = {L"FORMAT", RectXYWH(x, y, iw, 14)};             y += 18;
    formatSeg_.rect = RectXYWH(x, y, 140, 36);
    bitrateField_.rect = RectXYWH(x + 152, y, iw - 152, 36);        y += 36 + 16;
    captions_[3] = {L"OUTPUT", RectXYWH(x, y, iw, 14)};             y += 18;
    mixSeg_.rect = RectXYWH(x, y, iw, 36);                          y += 36 + 6;
    mixHintRect_ = RectXYWH(x, y, iw, 18);                          y += 18 + 14;
    shareRow_ = RectXYWH(x, y, iw, 18);                             y += 18;
    shareSlider_.rect = RectXYWH(x, y, iw, 24);                     y += 24 + 14;
    captions_[4] = {L"HOTKEYS", RectXYWH(x, y, iw, 14)};            y += 18;
    const float half = (iw - 12.0f) * 0.5f;
    keyBox_[0].rect = RectXYWH(x, y, half, 36);
    keyBox_[1].rect = RectXYWH(x + half + 12.0f, y, half, 36);      y += 36 + 4;
    hotkeyHintRect_ = RectXYWH(x, y, iw, 18);

    /* The transport bar. */
    const float by = transport_.top + (kTransportH - 48.0f) * 0.5f;
    startButton_.rect = RectXYWH(w - kMargin - 190, by, 190, 48);
    stopButton_.rect = RectXYWH(startButton_.rect.left - 12 - 110, by, 110, 48);
    messageArea_ = RectF(kMargin + 210, transport_.top, stopButton_.rect.left - kGap, h);
    showButton_.rect = RectXYWH(messageArea_.right - 140, by + 8, 140, 32);
}

/* ====================================================================== */
/* Painting                                                                */
/* ====================================================================== */

void MainView::Paint(Painter &p)
{
    p.Clear(theme::kBackground);
    PaintHeader(p);

    const bool recording = ctl_.GetPhase() == Phase::Recording;
    micView_.Paint(p, micCard_, kMicStyle, ctl_.Lane(StreamId::Microphone),
                   ctl_.StreamEnabled(StreamId::Microphone), recording, PaneOf(StreamId::Microphone));
    systemView_.Paint(p, systemCard_, kSystemStyle, ctl_.Lane(StreamId::System),
                      ctl_.StreamEnabled(StreamId::System), recording, PaneOf(StreamId::System));
    PaintPaneControls(p);
    PaintPanel(p);
    PaintTransport(p);

    for (Widget *w : all_) {
        if (!w->visible)
            continue;
        WidgetState st;
        st.hot = w == hot_ && (pressed_ == nullptr || pressed_ == w);
        st.pressed = w == pressed_ && w == hot_;        /* only while the pointer is still on it */
        st.focused = w == focus_ && focusVisible_;
        st.active = w == focus_;
        w->Paint(p, st);
        if (st.focused && !w->WantsText())          /* a text box shows its focus in its own outline */
            p.StrokeRound(Inflate(w->rect, 3.0f), theme::kControlRadius + 3.0f, theme::kFocusRing, 2.0f);
    }
}

/* A stream's transcript as its pane shows it: none, unless the stream has one. */
TranscriptPane MainView::PaneOf(StreamId stream) const
{
    TranscriptPane pane;
    if (ctl_.ShowsTranscript(stream)) {
        pane.lane = &ctl_.Transcript(stream);
        pane.status = ctl_.TranscriptionStatus(stream);
        pane.problem = &ctl_.TranscriptionProblem(stream);
    }
    return pane;
}

void MainView::PaintHeader(Painter &p)
{
    using namespace theme;
    p.Text(L"Audior-GUI", RectF(kMargin, 0, kMargin + 140, kHeaderH), Font::Title, kText);

    if (ctl_.IsSimulated()) {
        const D2D1_RECT_F badge = RectXYWH(kMargin + 150, (kHeaderH - 24) * 0.5f, 188, 24);
        p.StrokeRound(badge, 12.0f, WithAlpha(kWarn, 0.55f));
        p.Text(L"DEMO  ·  simulated audio", badge, Font::Label, kWarn, Align::Center);
    }

    /* Status, top right. */
    const D2D1_RECT_F pill = RectXYWH(width_ - kMargin - 132, (kHeaderH - 28) * 0.5f, 132, 28);
    p.FillRound(pill, 14.0f, kSurface);
    p.StrokeRound(pill, 14.0f, kBorder);
    D2D1_COLOR_F dot = kOk;
    const wchar_t *label = L"Ready";
    if (ctl_.GetPhase() == Phase::Recording) {
        /* The dot pulses once a second. */
        dot = WithAlpha(kRecord, 0.55f + 0.45f * std::fabs(std::sin(kPi * static_cast<float>(ctl_.Stats().seconds))));
        label = L"Recording";
    } else if (ctl_.LastOutcome().kind == Outcome::Kind::Failed) {
        dot = kError;
        label = L"Problem";
    }
    p.FillCircle(pill.left + 18, (pill.top + pill.bottom) * 0.5f, 5.0f, dot);
    p.Text(label, RectF(pill.left + 32, pill.top, pill.right - 8, pill.bottom), Font::BodyStrong, kText);
}

/* Each stream's gain label and value (the slider is a widget), and the microphone's Bluetooth note. */
void MainView::PaintPaneControls(Painter &p)
{
    if (micBluetooth_) {
        D2D1_RECT_F note = micNoteRect_;
        if (bluetoothLink_.visible)
            note.right = bluetoothLink_.rect.left - 12.0f;   /* the note gives way to the link */
        p.Text(BluetoothMicrophoneNote(), note, Font::Small, theme::kWarn);
    }
    for (const GainControl &gain : gain_) {
        const bool on = gain.slider.enabled;
        const D2D1_RECT_F &row = gain.row;
        p.Text(L"Gain", RectF(row.left, row.top, row.left + 36, row.bottom), Font::Body,
               on ? theme::kTextMuted : theme::kTextFaint);
        p.Text(gain.text, RectF(row.right - 50, row.top, row.right, row.bottom), Font::BodyStrong,
               on ? theme::kText : theme::kTextFaint, Align::Right);
    }
}

void MainView::PaintPanel(Painter &p)
{
    using namespace theme;
    p.FillRound(panel_, kCardRadius, kSurface);
    p.StrokeRound(panel_, kCardRadius, kBorder);

    for (const Caption &c : captions_)
        p.Text(c.text, c.rect, Font::Label, kTextFaint);

    p.Text(nameHint_, nameHintRect_, Font::Small, nameHintIsError_ ? kError : kTextFaint);
    p.Text(mixHint_, mixHintRect_, Font::Small, kTextFaint);
    if (shareSlider_.visible) {
        p.Text(L"Share of the mix", shareRow_, Font::Body, kTextMuted);
        p.Text(shareText_, shareRow_, Font::BodyStrong, kText, Align::Right);
    }

    if (ListeningForHotkey() && hotkeyProblem_.empty())
        p.Text(kHotkeyListenHint, hotkeyHintRect_, Font::Small, kTextMuted);
    else
        p.Text(hotkeyHint_, hotkeyHintRect_, Font::Small, hotkeyHintIsError_ ? kError : kTextFaint);
}

void MainView::PaintTransport(Painter &p)
{
    using namespace theme;
    p.Fill(transport_, kSurface);
    p.Line(0, transport_.top + 0.5f, width_, transport_.top + 0.5f, kBorder);

    /* Elapsed time and size, rebuilt only when their text would change. */
    const RecordingStats st = ctl_.Stats();
    const long long seconds = static_cast<long long>(st.seconds);
    if (seconds != timeKey_) {
        timeKey_ = seconds;
        timeText_ = FormatDuration(st.seconds);
    }
    const unsigned long long kib = st.bytesWritten >> 10;
    if (kib != sizeKey_) {
        sizeKey_ = kib;
        sizeText_ = FormatBytes(st.bytesWritten);
    }
    const bool recording = ctl_.GetPhase() == Phase::Recording;
    p.Text(timeText_, RectXYWH(kMargin, transport_.top + 10, 200, 50), Font::Time, recording ? kText : kTextMuted);
    p.Text(sizeText_, RectXYWH(kMargin + 2, transport_.top + 60, 200, 22), Font::Small, kTextFaint);

    /* What is going on, in words. */
    const Outcome &o = ctl_.LastOutcome();
    const RecordingSettings &s = ctl_.Settings();
    const float textRight = showButton_.visible ? showButton_.rect.left - kGap : messageArea_.right;
    const D2D1_RECT_F line1 = RectF(messageArea_.left, transport_.top + 22, textRight, transport_.top + 48);
    const D2D1_RECT_F line2 = RectF(messageArea_.left, transport_.top + 48, textRight, transport_.top + 72);
    if (recording) {
        p.Text(L"Recording", line1, Font::BodyStrong, kText);
        p.Text(summary_, line2, Font::Small, kTextMuted);
    } else if (!s.includeMicrophone && !s.includeSystemAudio) {      /* Start is off: say why */
        p.Text(L"Nothing to record", line1, Font::BodyStrong, kWarn);
        p.Text(L"Tick the microphone or system audio.", line2, Font::Small, kTextMuted);
    } else if (o.kind == Outcome::Kind::Failed) {
        /* "What: why (hr=...)": the what on the first line and the why on the second, so neither is cut short. */
        const std::size_t colon = o.message.find(L": ");
        if (colon == std::wstring::npos) {
            p.Text(o.message, line1, Font::BodyStrong, kError);
            p.Text(o.files.empty() ? L"Nothing was recorded." : L"The audio captured before the problem was kept.",
                   line2, Font::Small, kTextMuted);
        } else {
            p.Text(o.message.c_str(), static_cast<UINT32>(colon), line1, Font::BodyStrong, kError);
            p.Text(o.message.c_str() + colon + 2, static_cast<UINT32>(o.message.size() - colon - 2), line2,
                   Font::Small, kTextMuted);
        }
    } else if (o.kind == Outcome::Kind::Saved) {
        p.Text(L"Saved", line1, Font::BodyStrong, kOk);
        p.Text(savedName_, line2, Font::Small, kTextMuted);
    } else {
        p.Text(L"Ready to record", line1, Font::BodyStrong, kText);
        p.Text(summary_, line2, Font::Small, kTextMuted);
    }
}

/* ====================================================================== */
/* Input                                                                   */
/* ====================================================================== */

Widget *MainView::HitTest(float x, float y) const
{
    for (Widget *w : all_) {
        if (w->Hit(x, y))
            return w;
    }
    return nullptr;
}

bool MainView::MouseMove(float x, float y)
{
    Widget *over = HitTest(x, y);
    const bool changed = over != hot_ || pressed_ != nullptr;
    hot_ = over;
    if (pressed_)
        pressed_->Drag(x, y);
    return changed;
}

bool MainView::MouseDown(float x, float y)
{
    focusVisible_ = false;
    pressed_ = HitTest(x, y);
    FocusOn(pressed_);
    if (pressed_)
        pressed_->Press(x, y);
    return true;
}

bool MainView::MouseUp(float x, float y)
{
    Widget *w = pressed_;
    pressed_ = nullptr;
    if (!w)
        return false;
    w->Release(x, y, w->Hit(x, y));
    return true;
}

void MainView::MouseLeave()
{
    hot_ = nullptr;
}

void MainView::FocusOn(Widget *widget)
{
    if (focus_ && focus_ != widget)
        focus_->FocusLost();
    focus_ = widget;
}

void MainView::MoveFocus(int direction)
{
    int i = -1;
    for (int k = 0; k < kWidgetCount; ++k) {
        if (all_[k] == focus_)
            i = k;
    }
    if (i < 0 && direction < 0)
        i = 0;
    for (int step = 0; step < kWidgetCount; ++step) {
        i = (i + direction + kWidgetCount) % kWidgetCount;
        if (all_[i]->Interactive()) {
            FocusOn(all_[i]);
            return;
        }
    }
}

bool MainView::Char(wchar_t c)
{
    return focus_ && focus_->Interactive() && focus_->Char(c);
}

bool MainView::KeyDown(UINT vk, bool shift)
{
    if (focus_ && focus_->Interactive() && focus_->CapturesKeys()) {       /* every key, Tab and Space too */
        focusVisible_ = true;
        return focus_->Key(vk);
    }
    if (vk == VK_TAB) {
        focusVisible_ = true;
        MoveFocus(shift ? -1 : 1);
        return true;
    }
    if (focus_ && focus_->Interactive()) {
        focusVisible_ = true;
        return focus_->Key(vk);
    }
    return false;
}

}  // namespace audiorgui::ui
