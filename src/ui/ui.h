// Immediate-mode widgets on top of Gfx. The app redraws only when something changed (see App::run).
#pragma once
#include <string>
#include "gfx.h"

// Exact token set of the old app's CSS variables (index.css + ThemeContext), light/dark aware.
struct Theme {
    // Structural tokens
    Color bg = rgb(0x121212);           // --bg-dark
    Color bg_medium = rgb(0x1e1e1e);    // --bg-medium
    Color bg_light = rgb(0x2a2a2a);     // --bg-light
    Color panel = rgb(0x1e1e1e);        // --surface
    Color panel2 = rgb(0x2a2a2a);       // --surface-variant
    Color text = rgb(0xffffff);         // --text-primary
    Color sub = rgb(0xc7c7c7);          // --text-secondary (derived)
    Color muted = rgb(0x999999);        // --text-muted (derived)
    Color border_subtle = {255, 255, 255, 31};
    Color border = {255, 255, 255, 61};
    Color border_strong = {255, 255, 255, 102};
    Color hover = {255, 255, 255, 15};  // --hover-overlay (alpha overlay)
    Color active = {255, 255, 255, 33}; // --active-overlay
    Color scrim = {0, 0, 0, 140};
    // Accent tokens
    Color accent = rgb(0x00bcd4);       // --primary-color (contrast-adjusted)
    Color primary_light = rgb(0x4dd0e1), primary_dark = rgb(0x0097a7);
    Color secondary = rgb(0x03a9f4), secondary_light = rgb(0x4fc3f7), secondary_dark = rgb(0x0288d1);
    Color accent2 = rgb(0x00e5ff), accent2_light = rgb(0x84ffff);  // --accent-color / --accent-light
    Color accent_text = rgb(0x000000);  // --on-primary
    Color danger = rgb(0xef5350);
    bool light = false;
};

enum class Icon { Play, Pause, Next, Prev, Shuffle, Repeat, RepeatOne, Heart, HeartFilled, Volume, Mute, Queue, Home, Library, Settings, Search, Close, Back, Download, Plus };

struct Input {
    float mx = -1, my = -1;
    bool down = false;       // left button held
    bool pressed = false;    // went down this frame
    bool released = false;   // went up this frame
    float press_x = 0, press_y = 0;
    float wheel = 0;         // lines, positive = scroll up
    float hwheel = 0;        // horizontal wheel / shift+wheel
    std::string text;        // text typed this frame (UTF-8)
    int backspace = 0, del = 0, left = 0, right = 0, home = 0, end = 0, enter = 0, tab = 0, escape = 0;
    bool ctrl = false;
    void new_frame() { pressed = released = false; wheel = hwheel = 0; text.clear(); backspace = del = left = right = home = end = enter = tab = escape = 0; }
};

struct Scroll {
    float y = 0;
    void clamp(float content_h, float view_h) {
        float m = content_h > view_h ? content_h - view_h : 0;
        if (y > m) y = m;
        if (y < 0) y = 0;
    }
};

class Ui {
public:
    Ui(Gfx& g) : g(g) {}
    Gfx& g;
    Theme th;
    Input in;
    int f_body = -1, f_small = -1, f_title = -1, f_bold = -1;
    int active = 0;      // id of the widget being dragged / edited
    int focus = 0;       // text field focus
    bool wants_frames = false;  // set by widgets that animate (caret blink)
    size_t caret = 0;

    bool hover(Rect r) const { return r.contains(in.mx, in.my); }
    bool clicked(Rect r) const { return in.released && r.contains(in.mx, in.my) && r.contains(in.press_x, in.press_y); }

    bool button(Rect r, const std::string& label, bool primary = false);
    bool icon_button(Rect r, Icon ic, bool on = false, float pad = 0.25f);
    void icon(Icon ic, Rect r, Color c);
    // Returns true while the value changes (drag or click). v in [0,1].
    bool slider(int id, Rect r, float& v);
    bool text_field(int id, Rect r, std::string& s, const char* placeholder, bool password = false);
    void label(int font, float x, float y, const std::string& s, Color c, float max_w = 0) { g.text(font, x, y, s, c, max_w); }
    // Truncating text centred vertically in r.
    void label_in(int font, Rect r, const std::string& s, Color c);
};
