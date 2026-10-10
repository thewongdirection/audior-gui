#include "main_view.h"

#include <cmath>

#include "../core/format.h"

namespace audiorgui::ui {

namespace {

constexpr float kPi = 3.14159265358979f;

const LaneStyle kMicStyle    = {L"Microphone",  theme::kMic};
const LaneStyle kSystemStyle = {L"System audio", theme::kSystem};

constexpr float kOffLaneHeight = 76.0f;     /* the card shown for a stream that is off */

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

MainView::MainView(RecorderController &controller, IViewHost *host) : ctl_(controller), host_(host)
{
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

    formatSeg_.options = {L"MP3", L"WAV"};
    formatSeg_.onChange = [this](int i) {
        ctl_.Settings().format = i == 0 ? Format::Mp3 : Format::Wav;
        Refresh();
    };

    bitrateField_.style = Button::Style::Field;
    bitrateField_.onClick = [this] { ChooseBitrate(); };

    micField_.style = Button::Style::Field;
    micField_.onClick = [this] {
        ChooseDevice(DeviceKind::Microphone, &RecordingSettings::microphoneId, micField_);
    };

    gainSlider_.minimum = 0;
    gainSlider_.maximum = kMaxAmplifyPercent;
    gainSlider_.step = 5;
    gainSlider_.accent = theme::kMic;
    gainSlider_.onChange = [this](int v) {
        ctl_.Settings().micAmplifyPercent = v;
        Refresh();
    };

    systemToggle_.onChange = [this](bool on) {
        ctl_.Settings().includeSystemAudio = on;
        Refresh();
    };

    systemField_.style = Button::Style::Field;
    systemField_.onClick = [this] {
        ChooseDevice(DeviceKind::Playback, &RecordingSettings::systemDeviceId, systemField_);
    };

    mixSeg_.options = {L"Merged file", L"Separate files"};
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
        Refresh();
    };

    startButton_.text = L"Start recording";
    startButton_.style = Button::Style::Primary;
    startButton_.glyph = Button::Glyph::Record;
    startButton_.onClick = [this] {
        ctl_.Start();
        if (host_)
            host_->Wake();
        Refresh();
    };

    stopButton_.text = L"Stop";
    stopButton_.glyph = Button::Glyph::Stop;
    stopButton_.onClick = [this] {
        ctl_.Stop();
        Refresh();
    };

    showButton_.text = L"Show in folder";
    showButton_.onClick = [this] {
        const Outcome &o = ctl_.LastOutcome();
        if (host_ && !o.files.empty())
            host_->ShowInFolder(o.files[0]);
    };

    Widget *const order[] = {&folderField_, &formatSeg_, &bitrateField_, &micField_, &gainSlider_,
                             &systemToggle_, &systemField_, &mixSeg_, &shareSlider_,
                             &startButton_, &stopButton_, &showButton_};
    for (std::size_t i = 0; i < 12; ++i)
        all_[i] = order[i];

    Refresh();
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

std::wstring MainView::Summarize(const RecordingSettings &s) const
{
    std::wstring t = s.includeSystemAudio ? L"Microphone + system audio" : L"Microphone";
    t += L"  ·  ";
    t += s.format == Format::Mp3 ? L"MP3 " + std::to_wstring(s.mp3BitrateKbps) + L" kbps"
                                 : std::wstring(L"WAV 48 kHz");
    if (s.includeSystemAudio)
        t += s.mixMode == MixMode::Merge ? L"  ·  one merged file" : L"  ·  two files";
    return t;
}

void MainView::Refresh()
{
    const RecordingSettings &s = ctl_.Settings();
    const RecordingSettings &a = ctl_.Active();
    const bool unlocked = !ctl_.SettingsLocked();

    folderField_.text = s.outputDirectory.empty() ? L"Choose a folder…" : Tail(s.outputDirectory, 34);
    folderField_.enabled = unlocked;

    formatSeg_.selected = s.format == Format::Mp3 ? 0 : 1;
    formatSeg_.enabled = unlocked;
    bitrateField_.text = std::to_wstring(s.mp3BitrateKbps) + L" kbps";
    bitrateField_.enabled = unlocked && s.format == Format::Mp3;

    micField_.text = s.microphoneId.empty() ? L"System default" : DeviceName(DeviceKind::Microphone, s.microphoneId);
    micField_.enabled = unlocked;
    gainSlider_.value = s.micAmplifyPercent;
    gainSlider_.enabled = unlocked;
    gainText_ = L"+" + std::to_wstring(s.micAmplifyPercent) + L"%";

    systemToggle_.on = s.includeSystemAudio;
    systemToggle_.enabled = unlocked;
    systemField_.visible = mixSeg_.visible = s.includeSystemAudio;
    systemField_.text = s.systemDeviceId.empty() ? L"System default" : DeviceName(DeviceKind::Playback, s.systemDeviceId);
    systemField_.enabled = mixSeg_.enabled = unlocked;
    mixSeg_.selected = s.mixMode == MixMode::Merge ? 0 : 1;
    shareSlider_.visible = s.includeSystemAudio && s.mixMode == MixMode::Merge;
    shareSlider_.enabled = unlocked;
    shareSlider_.value = s.mergeMicPercent;
    shareText_ = L"Mic " + std::to_wstring(s.mergeMicPercent) + L"%  ·  System " +
                 std::to_wstring(100 - s.mergeMicPercent) + L"%";

    startButton_.enabled = ctl_.CanStart();
    stopButton_.enabled = ctl_.CanStop();
    stopButton_.style = ctl_.CanStop() ? Button::Style::Prominent : Button::Style::Secondary;

    const Outcome &o = ctl_.LastOutcome();
    const bool saved = o.kind == Outcome::Kind::Saved && !o.files.empty() && ctl_.CanStart();
    showButton_.visible = saved;
    savedName_.clear();
    if (saved) {
        savedName_ = FileName(o.files[0]);
        if (o.files.size() > 1)
            savedName_ += L"  +" + std::to_wstring(o.files.size() - 1) + L" more";
    }

    summary_ = Summarize(a);
    micDevice_ = DeviceName(DeviceKind::Microphone, a.microphoneId);
    systemDevice_ = DeviceName(DeviceKind::Playback, a.systemDeviceId);

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
    const std::vector<DeviceInfo> &devices = ctl_.Devices(kind);
    std::vector<std::wstring> items{L"System default"};
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

/* ====================================================================== */
/* Layout                                                                  */
/* ====================================================================== */

void MainView::Layout(float w, float h)
{
    using namespace theme;
    width_ = w;
    height_ = h;

    const float bottom = h - kTransportH - kGap;
    content_   = RectF(kMargin, kHeaderH, w - kMargin - kPanelW - kGap, bottom);
    panel_     = RectF(w - kMargin - kPanelW, kHeaderH, w - kMargin, bottom);
    transport_ = RectF(0, h - kTransportH, w, h);

    /* The settings panel, top to bottom. */
    const float pad = 18.0f, x = panel_.left + pad, iw = kPanelW - 2 * pad;
    float y = panel_.top + pad;
    captions_[0] = {L"SAVE TO", RectXYWH(x, y, iw, 14)};            y += 18;
    folderField_.rect = RectXYWH(x, y, iw, 36);                     y += 36 + 16;
    captions_[1] = {L"FORMAT", RectXYWH(x, y, iw, 14)};             y += 18;
    formatSeg_.rect = RectXYWH(x, y, 140, 36);
    bitrateField_.rect = RectXYWH(x + 152, y, iw - 152, 36);        y += 36 + 16;
    captions_[2] = {L"MICROPHONE", RectXYWH(x, y, iw, 14)};         y += 18;
    micField_.rect = RectXYWH(x, y, iw, 36);                        y += 36 + 10;
    gainRow_ = RectXYWH(x, y, iw, 18);                              y += 18;
    gainSlider_.rect = RectXYWH(x, y, iw, 24);                      y += 24 + 16;
    captions_[3] = {L"SYSTEM AUDIO", RectXYWH(x, y + 5, iw - 60, 14)};
    systemToggle_.rect = RectXYWH(x + iw - 44, y, 44, 24);          y += 24 + 12;
    systemField_.rect = RectXYWH(x, y, iw, 36);                     y += 36 + 10;
    mixSeg_.rect = RectXYWH(x, y, iw, 36);                          y += 36 + 10;
    shareRow_ = RectXYWH(x, y, iw, 18);                             y += 18;
    shareSlider_.rect = RectXYWH(x, y, iw, 24);

    /* The transport bar. */
    const float by = transport_.top + (kTransportH - 48.0f) * 0.5f;
    startButton_.rect = RectXYWH(w - kMargin - 190, by, 190, 48);
    stopButton_.rect = RectXYWH(startButton_.rect.left - 12 - 110, by, 110, 48);
    messageArea_ = RectF(kMargin + 210, transport_.top, stopButton_.rect.left - kGap, h);
    showButton_.rect = RectXYWH(messageArea_.right - 140, by + 8, 140, 32);
}

void MainView::LaneRects(D2D1_RECT_F *mic, D2D1_RECT_F *system) const
{
    const float total = Height(content_);
    if (ctl_.StreamEnabled(StreamId::System)) {
        const float each = (total - theme::kGap) * 0.5f;
        *mic = RectF(content_.left, content_.top, content_.right, content_.top + each);
        *system = RectF(content_.left, content_.bottom - each, content_.right, content_.bottom);
    } else {
        *mic = RectF(content_.left, content_.top, content_.right, content_.bottom - theme::kGap - kOffLaneHeight);
        *system = RectF(content_.left, content_.bottom - kOffLaneHeight, content_.right, content_.bottom);
    }
}

/* ====================================================================== */
/* Painting                                                                */
/* ====================================================================== */

void MainView::Paint(Painter &p)
{
    p.Clear(theme::kBackground);
    PaintHeader(p);

    D2D1_RECT_F micCard, systemCard;
    LaneRects(&micCard, &systemCard);
    const bool recording = ctl_.GetPhase() == Phase::Recording;
    micView_.Paint(p, micCard, kMicStyle, micDevice_, ctl_.Lane(StreamId::Microphone), recording);
    if (ctl_.StreamEnabled(StreamId::System))
        systemView_.Paint(p, systemCard, kSystemStyle, systemDevice_, ctl_.Lane(StreamId::System), recording);
    else
        LaneView::PaintOff(p, systemCard, kSystemStyle, L"Off — switch it on in the settings to record what you hear");

    PaintPanel(p);
    PaintTransport(p);

    for (Widget *w : all_) {
        if (!w->visible)
            continue;
        WidgetState st;
        st.hot = w == hot_ && (pressed_ == nullptr || pressed_ == w);
        st.pressed = w == pressed_;
        st.focused = w == focus_ && focusVisible_;
        w->Paint(p, st);
        if (st.focused)
            p.StrokeRound(Inflate(w->rect, 3.0f), theme::kControlRadius + 3.0f, theme::kFocusRing, 2.0f);
    }
}

void MainView::PaintHeader(Painter &p)
{
    using namespace theme;
    p.Text(L"audior", RectF(kMargin, 0, kMargin + 110, kHeaderH), Font::Title, kText);

    if (ctl_.IsSimulated()) {
        const D2D1_RECT_F pill = RectXYWH(kMargin + 92, (kHeaderH - 24) * 0.5f, 188, 24);
        p.StrokeRound(pill, 12.0f, WithAlpha(kWarn, 0.55f));
        p.Text(L"DEMO  ·  simulated audio", pill, Font::Label, kWarn, Align::Center);
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

void MainView::PaintPanel(Painter &p)
{
    using namespace theme;
    p.FillRound(panel_, kCardRadius, kSurface);
    p.StrokeRound(panel_, kCardRadius, kBorder);

    for (const Caption &c : captions_)
        p.Text(c.text, c.rect, Font::Label, kTextFaint);

    p.Text(L"Gain", gainRow_, Font::Body, kTextMuted);
    p.Text(gainText_, gainRow_, Font::BodyStrong, kText, Align::Right);
    if (shareSlider_.visible) {
        p.Text(L"Share of the mix", shareRow_, Font::Body, kTextMuted);
        p.Text(shareText_, shareRow_, Font::BodyStrong, kText, Align::Right);
    }
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
    const float textRight = showButton_.visible ? showButton_.rect.left - kGap : messageArea_.right;
    const D2D1_RECT_F line1 = RectF(messageArea_.left, transport_.top + 22, textRight, transport_.top + 48);
    const D2D1_RECT_F line2 = RectF(messageArea_.left, transport_.top + 48, textRight, transport_.top + 72);
    if (recording) {
        p.Text(L"Recording", line1, Font::BodyStrong, kText);
        p.Text(summary_, line2, Font::Small, kTextMuted);
    } else if (o.kind == Outcome::Kind::Failed) {
        p.Text(o.message, line1, Font::BodyStrong, kError);
        p.Text(o.files.empty() ? L"Nothing was recorded." : L"The audio captured before the problem was kept.",
               line2, Font::Small, kTextMuted);
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
    focus_ = pressed_;
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

void MainView::MoveFocus(int direction)
{
    const int count = 12;
    int i = -1;
    for (int k = 0; k < count; ++k) {
        if (all_[k] == focus_)
            i = k;
    }
    if (i < 0 && direction < 0)
        i = 0;
    for (int step = 0; step < count; ++step) {
        i = (i + direction + count) % count;
        if (all_[i]->Interactive()) {
            focus_ = all_[i];
            return;
        }
    }
}

bool MainView::KeyDown(UINT vk, bool shift)
{
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
