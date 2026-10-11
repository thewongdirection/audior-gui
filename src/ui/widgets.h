/*
 * widgets.h - The few controls the window needs, drawn with Direct2D.
 *
 * A widget owns its state and look; MainView owns the layout and wires the
 * callbacks.  Input arrives in DIPs.  Keyboard handling follows the usual
 * conventions (Space / Enter activate, arrows adjust) so the window is fully
 * usable without a mouse.
 */
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../core/hotkey.h"
#include "../core/line_editor.h"
#include "geometry.h"
#include "painter.h"

namespace audiorgui::ui {

struct WidgetState {
    bool hot     = false;       /* the pointer is over it                         */
    bool pressed = false;       /* the mouse button is held on it                 */
    bool focused = false;       /* it has the keyboard focus, shown as a ring     */
    bool active  = false;       /* it has the input focus (by mouse or keyboard)  */
};

class Widget {
public:
    D2D1_RECT_F rect    = {};
    bool        visible = true;
    bool        enabled = true;

    virtual ~Widget() = default;

    bool Interactive() const { return visible && enabled; }
    bool Hit(float x, float y) const { return Interactive() && Contains(rect, x, y); }

    virtual void Paint(Painter &painter, const WidgetState &state) const = 0;

    virtual void Press(float x, float y) { (void)x; (void)y; }              /* button down on it     */
    virtual void Drag(float x, float y) { (void)x; (void)y; }               /* moved while held      */
    virtual void Release(float x, float y, bool inside)                     /* button up             */
    {
        (void)x; (void)y; (void)inside;
    }
    virtual bool Key(UINT virtualKey) { (void)virtualKey; return false; }   /* true if it used the key */
    virtual bool Char(wchar_t c) { (void)c; return false; }                 /* a typed character       */
    virtual bool WantsText() const { return false; }                        /* edits text: I-beam cursor */
    virtual bool CapturesKeys() const { return false; }                     /* wants every key, Tab included */
    virtual void FocusLost() {}                                             /* the focus moved elsewhere     */
};

/* A push button, (Style::Field) a field that opens a choice when clicked, or (Style::Link) bare
 * text in the system-audio accent that acts when clicked, right-aligned in its rectangle. */
class Button : public Widget {
public:
    enum class Style { Primary, Prominent, Secondary, Field, Link };
    enum class Glyph { None, Record, Stop };

    std::wstring          text;
    Style                 style = Style::Secondary;
    Glyph                 glyph = Glyph::None;
    std::function<void()> onClick;

    void Paint(Painter &painter, const WidgetState &state) const override;
    void Release(float x, float y, bool inside) override;
    bool Key(UINT virtualKey) override;
};

/* A row of mutually exclusive options. */
class Segmented : public Widget {
public:
    std::vector<std::wstring> options;
    int                       selected = 0;
    std::function<void(int)>  onChange;

    void Paint(Painter &painter, const WidgetState &state) const override;
    void Press(float x, float y) override;
    bool Key(UINT virtualKey) override;

private:
    void Select(int index);
};

/* A checkbox, in the accent colour of the stream it controls. */
class Checkbox : public Widget {
public:
    bool                      checked = false;
    D2D1_COLOR_F              accent  = theme::kMic;
    std::function<void(bool)> onChange;

    void Paint(Painter &painter, const WidgetState &state) const override;
    void Release(float x, float y, bool inside) override;
    bool Key(UINT virtualKey) override;

private:
    void Flip();
};

/* A horizontal slider over an integer range. */
class Slider : public Widget {
public:
    int                      minimum = 0, maximum = 100, value = 0, step = 1;
    D2D1_COLOR_F             accent = theme::kMic;
    std::function<void(int)> onChange;

    void Paint(Painter &painter, const WidgetState &state) const override;
    void Press(float x, float y) override;
    void Drag(float x, float y) override;
    bool Key(UINT virtualKey) override;

private:
    void SetValue(int v);
    void SetFromX(float x);
};

/* A single-line text field.  The editing rules live in LineEditor; this draws
 * the text, caret and selection and turns the mouse and keyboard into edits.
 * The text scrolls to keep the caret in view. */
class TextBox : public Widget {
public:
    /* `accepts` filters typed and pasted characters (null: all are taken). */
    explicit TextBox(LineEditor::Accepts accepts = nullptr, std::size_t maxChars = 120)
        : editor(maxChars, std::move(accepts)) {}

    LineEditor    editor;
    std::wstring  placeholder;                  /* shown while the box is empty            */
    bool          invalid = false;              /* outline the box as a problem            */
    std::function<void(const std::wstring &)> onChange;     /* the user changed the text   */
    std::function<std::wstring()>             clipboardGet;
    std::function<void(const std::wstring &)> clipboardSet;

    /* Sets the text without calling onChange (nothing happens if it is the same). */
    void SetText(const std::wstring &text);

    void Paint(Painter &painter, const WidgetState &state) const override;
    void Press(float x, float y) override;
    void Drag(float x, float y) override;
    bool Key(UINT virtualKey) override;
    bool Char(wchar_t c) override;
    bool WantsText() const override { return true; }

private:
    void        Edited(bool changed);
    void        EnsureLayout(Painter &painter) const;
    float       XOf(std::size_t position) const;            /* from the text's left edge */
    std::size_t PositionAt(float x) const;

    mutable ComPtr<IDWriteTextLayout> layout_;      /* of layoutText_, rebuilt when the text changes */
    mutable std::wstring              layoutText_;
    mutable float                     scroll_ = 0.0f;       /* how far the text is scrolled left */
};

/* A keyboard shortcut: what it does, and the keys.  Activating it (click, Enter
 * or Space) makes it listen; the next key combination typed is offered to
 * onCapture, which says whether it took it (else the box keeps listening, so a
 * refused combination can be replaced at once).  Escape gives up. */
class KeyBox : public Widget {
public:
    std::wstring label;                         /* what the shortcut does: "Start"          */
    std::wstring text;                          /* the keys: "Ctrl+Alt+R"                   */
    bool         invalid = false;               /* outline the box as a problem             */
    std::function<void()>               onBegin;        /* it started listening             */
    std::function<bool(const Hotkey &)> onCapture;

    bool Capturing() const { return capturing_; }
    void Capture(const Hotkey &hotkey);         /* a combination typed (or caught by the system) while listening */

    void Paint(Painter &painter, const WidgetState &state) const override;
    void Release(float x, float y, bool inside) override;
    bool Key(UINT virtualKey) override;
    bool CapturesKeys() const override { return capturing_; }
    void FocusLost() override { capturing_ = false; }

private:
    void Begin();

    bool capturing_ = false;
};

}  // namespace audiorgui::ui
