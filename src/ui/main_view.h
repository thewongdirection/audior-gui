/*
 * main_view.h - The application window's content: layout, painting and input.
 *
 *   +----------------------------------------------------------------+
 *   |  audior  [DEMO]                                      * Ready   |  header
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
 *                                              +---------------------+
 *   |  00:00:12   Recording             [ Stop ] [ Start recording ] |
 *   +----------------------------------------------------------------+
 *
 * Each stream has its own pane: a checkbox that says whether it is recorded,
 * a drop-down of every detected device, its gain and its waveform.  When the
 * engine can transcribe, a "Transcript" switch beside the title adds the live
 * text under the waveform.  MainView only reads
 * and drives a RecorderController; everything the operating system has to do
 * (dialogs, menus, the frame timer) goes through IViewHost, so the same view
 * renders into a window or into an offscreen bitmap for screenshots.
 */
#pragma once

#include <string>
#include <vector>

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
};

class MainView {
public:
    /* `host` may be null (offscreen rendering); dialogs then do nothing. */
    MainView(RecorderController &controller, IViewHost *host);

    void Layout(float width, float height);

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

    bool HandCursor() const { return hot_ && hot_->Interactive() && !hot_->WantsText(); }
    bool TextCursor() const { return hot_ && hot_->Interactive() && hot_->WantsText(); }
    bool Capturing() const { return pressed_ != nullptr; }

private:
    static constexpr int kWidgetCount = 17;
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
    void ConfigureGain(StreamId stream, int RecordingSettings::*percent, const D2D1_COLOR_F &accent);
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
    Switch    micTranscript_, systemTranscript_;
    Button    micField_, systemField_, folderField_, bitrateField_;
    Button    startButton_, stopButton_, showButton_;
    Segmented formatSeg_, mixSeg_;
    Slider    shareSlider_;
    TextBox   fileNameBox_;
    GainControl gain_[kStreamCount];
    Widget   *all_[kWidgetCount];       /* tab order */

    Widget *hot_ = nullptr, *pressed_ = nullptr, *focus_ = nullptr;
    bool    focusVisible_ = false;      /* the focus ring shows only for keyboard use */

    D2D1_RECT_F micCard_ = {}, systemCard_ = {}, panel_ = {}, transport_ = {}, messageArea_ = {};
    D2D1_RECT_F shareRow_ = {}, mixHintRect_ = {}, nameHintRect_ = {};
    Caption     captions_[4] = {};

    LaneView micView_, systemView_;

    /* Text that changes rarely is built when it changes, not every frame. */
    std::wstring shareText_, mixHint_, nameHint_, summary_, savedName_;
    bool         nameHintIsError_ = false;
    std::wstring timeText_, sizeText_;
    long long          timeKey_ = -1;
    unsigned long long sizeKey_ = ~0ULL;
    Phase              lastPhase_ = Phase::Idle;
};

}  // namespace audiorgui::ui
