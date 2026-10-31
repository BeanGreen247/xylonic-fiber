// Application shell: owns the window, the UI, the player and the session. Single UI thread; network on workers.
#pragma once
#include <SDL.h>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "../audio/player.h"
#include "../net/discovery.h"
#include "../subsonic/client.h"
#include <atomic>
#include "../ui/themes.h"
#include "../ui/ui.h"
#include "config.h"
#include "downloads.h"
#include "model_json.h"
#include "store.h"
#include "images.h"
#include "queue.h"
#include "skin.h"
#include "worker.h"

constexpr const char* kVersion = "0.1.0";
constexpr const char* kBuildDate = __DATE__;
enum WidgetId { kUrl = 1, kUser, kPass, kSearch, kPanelSearch, kPlNew, kPlRename, kEditName, kEditHex, kSeek = 10, kVol };

struct AppOptions {
    std::string server, user, pass;  // optional auto-login (testing)
    std::string shot_path;           // write a BMP of the first stable frame and exit
    int shot_after_ms = 1500;
    std::string nav;                 // home|library|queue|settings|search (testing)
    bool autoplay = false;
    std::string search;     // type this into the search field after login (testing)
    bool help = false;      // open the keyboard help dialog (testing)
    bool pulse = false;     // Pulse Deck on (testing)
    bool carmode = false;   // open Car Mode (testing)
    bool nowplaying = false; // open Now Playing (testing)
    std::string panel;      // open the right panel: queue|history|playlists (testing)
    bool selftest = false;  // init and exit
    bool offline = false;   // start in offline mode with the saved account
    std::string download_album;  // enqueue this album after login (testing)
    int width = 1024, height = 768;
};

class App {
public:
    explicit App(AppOptions o);
    ~App();
    bool init();
    int run();

private:
    enum class Screen { Login, Main };
    enum class PanelTab : uint8_t { Queue, History, Playlists };
    struct Playlist { std::string id, name, server_id, owner; std::vector<Song> songs; int64_t created = 0, updated = 0; };
    struct HistoryEntry { Song song; int64_t at = 0; };
    enum class Nav { Home, Library, Search, Queue, Downloads, Settings };
    enum class ViewKind { Root, Artist, Album };
    struct View { ViewKind kind = ViewKind::Root; std::string id, title; Scroll scroll; };

    // events / frame
    void handle(const SDL_Event& e);
    void frame();
    void draw_login(float W, float H);
    void start_scan();
    void test_connection();
    void select_connection(int idx);
    void draw_main(float W, float H);
    void draw_player_bar(Rect r);
    void draw_library(Rect r);
    void draw_artist(Rect r);
    void draw_album(Rect r);
    void draw_search(Rect r);
    void draw_queue(Rect r);
    void draw_settings(Rect r);
    void draw_downloads(Rect r);
    void draw_song_rows(Rect r, const std::vector<Song>& songs, Scroll& sc, bool show_cover, int base_id);
    float draw_album_grid(Rect r, const std::vector<Album>& albums, float y, float& hit_scroll_y);
    void push_view(ViewKind k, const std::string& id, const std::string& title);
    void pop_view();
    void set_nav(Nav n);
    void mark() { dirty_ = true; }
    // Fonts (Roboto 400/500/700 + Font Awesome), created lazily per size. weight 600 maps to Bold.
    int F(int weight, float px, bool italic = false);
    int FA(float px);
    // CSS-like text: y_top/line_height in logical px, glyphs centred in the line box. Returns drawn width.
    float T(int weight, float px, float x, float y_top, float line_h, const std::string& s, Color c, float max_w = 0, bool italic = false);
    void fa(uint32_t cp, float cx, float cy, float px, Color c);   // glyph centred on (cx, cy)
    void svg(int which, Rect r, Color tint);
    // Shell
    void draw_header(float W);
    void draw_right_panel(float W, float H);
    void draw_modal(float W, float H);
    void draw_ctx_menu(float W, float H);
    void draw_help(float W, float H);
    void open_ctx(const Song& s, float x, float y);
    bool ctx_hit(Rect r, const Song& s);
    int dropdown(int id, Rect anchor, const std::vector<std::string>& opts, int current);
    void set_sleep_timer(int minutes);
    std::string sleep_remaining() const;
    void tick_sleep();
    void wipe_all_data();
    void apply_perf_mode();
    void draw_now_playing(float W, float H);
    void draw_car_mode(float W, float H);
    void set_car_mode(bool on);
    void draw_pulse_deck(Rect r);
    void toggle_panel(PanelTab tab);
    void pl_load();
    void pl_save();
    Playlist* pl_find(const std::string& id);
    std::string pl_create(const std::string& name, const std::vector<Song>& songs);
    void pl_delete(const std::string& id);
    void pl_rename(const std::string& id, const std::string& name);
    bool pl_add_song(const std::string& id, const Song& song);
    void pl_remove_song(const std::string& id, int index);
    void pl_sync();
    void draw_playlists_tab(Rect panel, float content_y);
    void draw_add_to_playlist(float W, float H);
    void history_load();
    void history_save();
    void history_add(const Song& s);
    void play_random_mix();
    void load_albums_page(int page);
    void load_songs_page(int page);
    void load_liked();
    void load_song_count();
    void fetch_all_songs(bool shuffle_play);
    void draw_nav(float H);
    void draw_discover(Rect r);
    bool hit(Rect r) const { return ui_->clicked(r); }
    ThemeDef theme_def(int idx) const;
    void apply_theme();

    // session
    void connect(bool trust_pin);
    void on_connected();
    void enter_offline();
    void open_account_data();
    void refresh_dl_state();
    void logout();
    void load_home();
    void load_artists();
    void load_artist(const std::string& id);
    void load_album(const std::string& id);
    void run_search();
    std::string cover_key(const std::string& id, int px) const { return id + "@" + std::to_string(px); }
    SDL_Texture* cover(const std::string& id, int px);
    void prefetch_covers(const std::vector<Song>& songs);  // for offline browsing

    // playback
    void play_songs(const std::vector<Song>& songs, size_t start, bool shuffle);
    void start_current();
    void toggle_play();
    void next_track();
    void prev_track();
    void tick_player();
    void preload_next();
    void set_volume(float v);
    void save_queue_state();
    std::string track_kind(const Song& s) const;
    std::string perm_path(const Song& s) const;
    std::string stream_path(const Song& s) const;

    AppOptions opt_;
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    std::unique_ptr<Gfx> gfx_;
    std::unique_ptr<Ui> ui_;
    std::unique_ptr<Worker> worker_;
    std::unique_ptr<ImageCache> images_;
    Player player_;
    Queue queue_;
    Config cfg_;
    std::string pref_, cfg_path_, perm_dir_, stream_dir_, img_dir_;
    float S_ = 1.0f;  // device pixels per logical unit
    bool dirty_ = true, running_ = true;
    uint32_t shot_deadline_ = 0;

    Screen screen_ = Screen::Login;
    Nav nav_ = Nav::Home;
    std::vector<View> views_;  // top = current; bottom = nav root
    std::shared_ptr<SubsonicClient> client_;
    ConnectOptions copt_;
    bool offline_ = false;
    std::string acct_;
    std::unique_ptr<Store> store_;
    std::unique_ptr<Downloads> downloads_;
    struct DlInfo { int state; int64_t bytes, total; };
    std::unordered_map<std::string, DlInfo> dl_state_;
    bool dl_dirty_ = true;
    struct Section { std::vector<Album> albums; bool loading = true; std::string error; float scroll_x = 0; };
    Section sections_[4];   // newest, recent, frequent, random
    Scroll discover_scroll_;
    std::unordered_map<int, int> font_ids_;
    std::string assets_dir_;
    int svg_px_[3] = {0, 0, 0};
    bool show_help_ = false, playing_random_ = false;
    SDL_Texture* svg_tex_[3] = {nullptr, nullptr, nullptr};  // logo, github, lastfm
    void load_section(int i);
    enum class Modal { None, Sleep, Theme, ConfirmWipe };
    Modal modal_ = Modal::None;
    uint64_t sleep_end_ms_ = 0;
    int dropdown_open_ = 0;
    std::function<void()> dropdown_draw_;
    std::pair<int, int> dropdown_choice_{0, -1};
    float pulse_caps_[24] = {};
    uint32_t pulse_last_ms_ = 0, pulse_frames_ = 0, pulse_toast_ms_ = 0;
    float pulse_avg_ms_ = 16.0f, eq_bass_db_ = 0, eq_treble_db_ = 0;
    bool pulse_off_ = false, pulse_drag_ = false;
    bool car_open_ = false, car_flip_ = false, car_layout_min_ = false;
    bool np_open_ = false;
    Scroll np_scroll_;
    bool panel_open_ = false, panel_search_open_ = false;
    PanelTab panel_tab_ = PanelTab::Queue;
    std::string panel_search_;
    std::vector<HistoryEntry> history_;
    std::vector<Playlist> playlists_;
    std::set<std::string> pl_syncing_;
    std::string pl_new_name_, pl_expanded_, pl_rename_id_, pl_rename_text_;
    bool pl_synced_once_ = false, pl_sync_busy_ = false;
    std::optional<Song> add_song_, ctx_song_;
    float ctx_x_ = 0, ctx_y_ = 0;
    Scroll panel_scroll_;
    int panel_drag_ = -1;
    int lib_mode_ = 0, lib_page_[4] = {0, 0, 0, 0};
    std::vector<Album> alb_items_; bool alb_more_ = false, alb_loading_ = false; int alb_loaded_page_ = -1;
    std::vector<Song> song_items_, liked_; bool song_more_ = false, song_loading_ = false, liked_loaded_ = false, liked_loading_ = false, busy_all_ = false;
    int song_loaded_page_ = -1, total_songs_ = -1;
    Scroll dl_scroll_, settings_scroll_;
    int edit_slot_ = 0;
    std::string edit_name_, edit_hex_;
    bool edit_loaded_ = false;
    uint32_t session_ = 0;

    // login form
    std::string f_url_, f_user_, f_pass_, login_msg_, cert_fp_;
    bool connecting_ = false, cert_prompt_ = false;
    bool testing_ = false, test_ok_ = false, show_pass_ = false, offline_check_ = false, scanning_ = false, picker_open_ = false;
    int sel_conn_ = -1;
    Scroll login_scroll_;
    uint32_t scan_gen_ = 0;
    std::atomic<bool> scan_cancel_{false};
    std::string login_pin_;
    std::vector<DiscoveredServer> discovered_;

    // data
    std::vector<Album> recent_, random_;
    std::vector<Artist> artists_;
    std::unordered_map<std::string, Artist> artist_cache_;
    std::unordered_map<std::string, Album> album_cache_;
    std::string load_err_;
    bool loading_home_ = false, loading_artists_ = false;
    std::string search_q_, search_done_q_;
    SubsonicClient::SearchAll search_res_;
    Nav search_prev_nav_ = Nav::Library;
    bool search_pending_ = false, search_loading_ = false;
    uint32_t search_due_ = 0;
    Scroll search_scroll_, queue_scroll_, home_scroll_, lib_scroll_, album_scroll_;

    // playback state
    uint32_t play_gen_ = 0;
    std::string preloaded_id_;
    bool preloading_ = false, speed_popup_ = false;
    bool track_loading_ = false;
    std::string play_err_;
    float vol_ = 0.8f, seek_drag_v_ = 0;
    bool seek_dragging_ = false;
    int shown_sec_ = -1;
    uint32_t last_tick_ = 0;
    std::string scrobbled_id_;
    uint32_t queue_saved_rev_ = 0;
};
