/*
 * main_view.h - The application window's content: layout, painting and input.
 *
 *   +----------------------------------------------------------------+
 *   |  audior  [DEMO]                                      * Ready   |  header
 *   +------------------------------------------+---------------------+
 *   |  Microphone         waveform      -12 dB |  SAVE TO            |
 *   |  ||| |||||| |||| |||||| ||||||||          |  FORMAT             |
 *   +------------------------------------------+  MICROPHONE         |
 *   |  System audio       waveform      -20 dB |  SYSTEM AUDIO       |
 *   |  ||| |||||| |||| |||||| ||||||||          |  ...                |
 *   +------------------------------------------+---------------------+
 *   |  00:00:12   Recording ...             [ Stop ] [ Start recording ] |
 *   +----------------------------------------------------------------+
 *
 * MainView only reads and drives a RecorderController; everything the
 * operating system has to do (dialogs, menus, the frame timer) goes through
 * IViewHost, so the same view renders into a window or into an offscreen
 * bitmap for screenshots.
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
    bool KeyDown(UINT virtualKey, bool shift);

    bool HandCursor() const { return hot_ && hot_->Interactive(); }
    bool Capturing() const { return pressed_ != nullptr; }

private:
    struct Caption { const wchar_t *text; D2D1_RECT_F rect; };

    void Refresh();
    void ChooseDevice(DeviceKind kind, std::wstring RecordingSettings::*idField, const Widget &anchor);
    void ChooseBitrate();
    std::wstring DeviceName(DeviceKind kind, const std::wstring &id) const;
    std::wstring Summarize(const RecordingSettings &settings) const;
    Widget *HitTest(float x, float y) const;
    void MoveFocus(int direction);
    void LaneRects(D2D1_RECT_F *mic, D2D1_RECT_F *system) const;

    void PaintHeader(Painter &p);
    void PaintPanel(Painter &p);
    void PaintTransport(Painter &p);

    RecorderController &ctl_;
    IViewHost          *host_;
    float               width_ = 0.0f, height_ = 0.0f;

    Button    folderField_, bitrateField_, micField_, systemField_;
    Button    startButton_, stopButton_, showButton_;
    Segmented formatSeg_, mixSeg_;
    Toggle    systemToggle_;
    Slider    gainSlider_, shareSlider_;
    Widget   *all_[12];                 /* tab order */

    Widget *hot_ = nullptr, *pressed_ = nullptr, *focus_ = nullptr;
    bool    focusVisible_ = false;      /* the focus ring shows only for keyboard use */

    D2D1_RECT_F content_ = {}, panel_ = {}, transport_ = {}, messageArea_ = {};
    D2D1_RECT_F gainRow_ = {}, shareRow_ = {};
    Caption     captions_[4] = {};

    LaneView micView_, systemView_;

    /* Text that changes rarely is built when it changes, not every frame. */
    std::wstring micDevice_, systemDevice_, gainText_, shareText_, summary_, savedName_;
    std::wstring timeText_, sizeText_;
    long long          timeKey_ = -1;
    unsigned long long sizeKey_ = ~0ULL;
    Phase              lastPhase_ = Phase::Idle;
};

}  // namespace audiorgui::ui
