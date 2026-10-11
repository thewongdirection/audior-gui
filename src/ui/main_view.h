/*
 * main_view.h - The application window's content: layout, painting and input.
 *
 *   +----------------------------------------------------------------+
 *   |  Audior-GUI  [DEMO]                                  * Ready   |  header
 *   +------------------------------------------+---------------------+
 *   |  [x] Microphone                  -12 dB  |  SAVE TO            |
 *   |  [device v]        Gain ---o--- +0%      |  [folder        v]  |
 *   |  ||| |||||| |||| |||||| ||||||||          |  FILE NAME          |
 *   +------------------------------------------+  [meeting        ]  |
 *   |  [x] System audio                -20 dB  |  FORMAT             |
 *   |  [device v]        Gain ---o--- +0%      |  [MP3|WAV] [128 v]  |
 *   |  ||| |||||| |||| |||||| ||||||||          |  OUTPUT             |
 *   +------------------------------------------+  [merged|separate]  |
 *                                              |  Mic 65% . Sys 35%  |
 *                                              |  HOTKEYS            |
 *                                              |  [Start Ctrl+Alt+R] |
 *                                              |  [Stop  Ctrl+Alt+S] |
 *                                              +---------------------+
 *   |  00:00:12   Recording             [ Stop ] [ Start recording ] |
 *   +----------------------------------------------------------------+
 *
 * Each stream has its own pane: a checkbox that says whether it is recorded,
 * a drop-down of every detected device, its gain and its waveform.  When the
 * engine can transcribe, a "Transcript" switch beside the title adds the live
 * text under the waveform, and the panel gets the choice of model and language.
 * MainView only reads and drives a RecorderController; everything the operating system has to do
 * (dialogs, menus, the frame timer, the system-wide hotkeys) goes through IViewHost, so the same
 * view renders into a window or into an offscreen bitmap for screenshots.
 */
#pragma once

#include <string>
#include <vector>

#include "../core/hotkey.h"
#include "../core/recorder_controller.h"
#include "lane_view.h"
#include "widgets.h"

namespace audiorgui::ui {

class IViewHost {
public:
    virtual ~IViewHost() = default;

    /* A folder chosen by the user, or empty if cancelled. */
    virtual std::wstring PickFolder(const std::wstring &current) = 0;

    /* Show `items` in a menu under `anchor` (DIPs); the chosen index or -1. */
    virtual int Choose(const std::vector<std::wstring> &items, int selected,
                       const D2D1_RECT_F &anchor) = 0;

    virtual void ShowInFolder(const std::wstring &file) = 0;

    virtual std::wstring ClipboardText() = 0;
    virtual void         SetClipboardText(const std::wstring &text) = 0;

    /* Something may be animating: make sure frames keep coming. */
    virtual void Wake() = 0;

    /* Makes `hotkey` the system-wide shortcut for `action` and remembers it for
     * the next run.  False, with the old shortcut still in force, if another
     * program already owns it. */
    virtual bool SetHotkey(HotkeyAction action, const Hotkey &hotkey) = 0;
};

class MainView {
public:
    /* `host` may be null (offscreen rendering); dialogs then do nothing. */
    MainView(RecorderController &controller, IViewHost *host);

    void Layout(float width, float height);

    /* How much taller than the window's base minimum it must be for every control of the settings
     * panel to fit: the Transcript section exists only when transcription is installed. */
    float ExtraMinHeight() const;

    /* Call after each RecorderController::Tick(). */
    void Update();

    void Paint(Painter &painter);

    /* Input, in DIPs.  Each returns true if the view needs repainting. */
    bool MouseMove(float x, float y);
    bool MouseDown(float x, float y);
    bool MouseUp(float x, float y);
    void MouseLeave();
    void CancelPress() { pressed_ = nullptr; }      /* the mouse capture was lost */
    bool KeyDown(UINT virtualKey, bool shift);
    bool Char(wchar_t c);                           /* a typed character, for the text box */

    void FocusFileName() { focus_ = &fileNameBox_; }       /* for screenshots */
    void ListenForHotkey(HotkeyAction action);             /* for screenshots */

    bool HandCursor() const { return hot_ && hot_->Interactive() && !hot_->WantsText(); }
    bool TextCursor() const { return hot_ && hot_->Interactive() && hot_->WantsText(); }
    bool Capturing() const { return pressed_ != nullptr; }

    /* What the Start and Stop buttons do (also for the hotkeys and the tray menu). */
    bool CanStart() const { return startButton_.Interactive(); }
    void StartRecording();
    void StopRecording();

    /* The hotkeys in force, and whether the system accepted each one. */
    void SetHotkey(HotkeyAction action, const Hotkey &hotkey, bool registered);

    /* A shortcut box is waiting for a key combination, which then belongs to
     * it, however it arrived (typed, or caught by the system as a hotkey). */
    bool ListeningForHotkey() const;
    void OfferHotkey(const Hotkey &hotkey);
    void WindowDeactivated() { if (focus_) focus_->FocusLost(); }

private:
    static constexpr int kWidgetCount = 23;
    struct Caption { const wchar_t *text; D2D1_RECT_F rect; };

    /* A stream's gain: the slider, where it sits, and its value as text. */
    struct GainControl {
        Slider       slider;
        D2D1_RECT_F  row = {};
        std::wstring text;
    };

    void Refresh();
    void ChooseDevice(DeviceKind kind, std::wstring RecordingSettings::*idField, const Widget &anchor);
    void ChooseBitrate();
    void ChooseModel();
    const TranscriptModel *SelectedModel() const;
    void ConfigureGain(StreamId stream, int RecordingSettings::*percent, const D2D1_COLOR_F &accent);
    void ConfigureHotkey(HotkeyAction action, const wchar_t *label);
    bool ChangeHotkey(HotkeyAction action, const Hotkey &candidate);
    void RefreshHotkeys();
    void FocusOn(Widget *widget);
    std::wstring DeviceName(DeviceKind kind, const std::wstring &id) const;
    std::wstring DeviceFieldText(DeviceKind kind, const std::wstring &id) const;
    std::wstring Summarize(const RecordingSettings &settings) const;
    Widget *HitTest(float x, float y) const;
    void MoveFocus(int direction);
    void LayoutPane(const D2D1_RECT_F &card, Checkbox &include, Switch &transcript, Button &device);
    TranscriptPane PaneOf(StreamId stream) const;

    void PaintHeader(Painter &p);
    void PaintPaneControls(Painter &p);
    void PaintPanel(Painter &p);
    void PaintTransport(Painter &p);

    RecorderController &ctl_;
    IViewHost          *host_;
    float               width_ = 0.0f;

    Checkbox  micCheck_, systemCheck_;
    Switch    micTranscript_, systemTranscript_, gpuSwitch_;
    Button    micField_, systemField_, folderField_, bitrateField_, modelField_, bluetoothLink_;
    Button    startButton_, stopButton_, showButton_;
    Segmented formatSeg_, mixSeg_, languageSeg_;
    Slider    shareSlider_;
    TextBox   fileNameBox_;
    KeyBox    keyBox_[kHotkeyActionCount];
    GainControl gain_[kStreamCount];
    Widget   *all_[kWidgetCount];       /* tab order */

    Widget *hot_ = nullptr, *pressed_ = nullptr, *focus_ = nullptr;
    bool    focusVisible_ = false;      /* the focus ring shows only for keyboard use */

    Hotkey hotkeys_[kHotkeyActionCount] = {kDefaultStartHotkey, kDefaultStopHotkey};
    bool   hotkeyActive_[kHotkeyActionCount] = {true, true};    /* the system accepted it */

    D2D1_RECT_F micCard_ = {}, systemCard_ = {}, panel_ = {}, transport_ = {}, messageArea_ = {};
    D2D1_RECT_F shareRow_ = {}, mixHintRect_ = {}, nameHintRect_ = {}, hotkeyHintRect_ = {}, micNoteRect_ = {};
    bool        micBluetooth_ = false;  /* the microphone to be recorded is a Bluetooth one: say what that does */
    Caption     captions_[6] = {};

    LaneView micView_, systemView_;

    /* Text that changes rarely is built when it changes, not every frame. */
    std::wstring shareText_, mixHint_, nameHint_, summary_, savedName_, hotkeyProblem_, hotkeyHint_;
    bool         nameHintIsError_ = false, hotkeyHintIsError_ = false;
    std::wstring timeText_, sizeText_;
    long long          timeKey_ = -1;
    unsigned long long sizeKey_ = ~0ULL;
    Phase              lastPhase_ = Phase::Idle;
};

}  // namespace audiorgui::ui
