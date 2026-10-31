// Application shell: owns the window, the UI, the player and the session. Single UI thread; network on workers.
#pragma once
#include <SDL.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../audio/player.h"
#include "../subsonic/client.h"
#include "../ui/ui.h"
#include "config.h"
#include "images.h"
#include "queue.h"
#include "worker.h"

enum WidgetId { kUrl = 1, kUser, kPass, kSearch, kSeek = 10, kVol };

struct AppOptions {
    std::string server, user, pass;  // optional auto-login (testing)
    std::string shot_path;           // write a BMP of the first stable frame and exit
    int shot_after_ms = 1500;
    std::string nav;                 // home|library|queue|settings|search (testing)
    bool autoplay = false;
    bool selftest = false;  // init and exit
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
    enum class Nav { Home, Library, Search, Queue, Settings };
    enum class ViewKind { Root, Artist, Album };
    struct View { ViewKind kind = ViewKind::Root; std::string id, title; Scroll scroll; };

    // events / frame
    void handle(const SDL_Event& e);
    void frame();
    void draw_login(float W, float H);
    void draw_main(float W, float H);
    void draw_sidebar(Rect r);
    void draw_player_bar(Rect r);
    void draw_home(Rect r);
    void draw_library(Rect r);
    void draw_artist(Rect r);
    void draw_album(Rect r);
    void draw_search(Rect r);
    void draw_queue(Rect r);
    void draw_settings(Rect r);
    void draw_song_rows(Rect r, const std::vector<Song>& songs, Scroll& sc, bool show_cover, int base_id);
    float draw_album_grid(Rect r, const std::vector<Album>& albums, float y, float& hit_scroll_y);
    void push_view(ViewKind k, const std::string& id, const std::string& title);
    void pop_view();
    void set_nav(Nav n);
    void mark() { dirty_ = true; }

    // session
    void connect(bool trust_pin);
    void on_connected();
    void logout();
    void load_home();
    void load_artists();
    void load_artist(const std::string& id);
    void load_album(const std::string& id);
    void run_search();
    std::string cover_key(const std::string& id, int px) const { return id + "@" + std::to_string(px); }
    SDL_Texture* cover(const std::string& id, int px);

    // playback
    void play_songs(const std::vector<Song>& songs, size_t start, bool shuffle);
    void start_current();
    void toggle_play();
    void next_track();
    void prev_track();
    void tick_player();
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
    uint32_t session_ = 0;

    // login form
    std::string f_url_, f_user_, f_pass_, login_msg_, cert_fp_;
    bool connecting_ = false, cert_prompt_ = false;

    // data
    std::vector<Album> recent_, random_;
    std::vector<Artist> artists_;
    std::unordered_map<std::string, Artist> artist_cache_;
    std::unordered_map<std::string, Album> album_cache_;
    std::string load_err_;
    bool loading_home_ = false, loading_artists_ = false;
    std::string search_q_;
    std::vector<Song> search_res_;
    Scroll search_scroll_, queue_scroll_, home_scroll_, lib_scroll_, album_scroll_;

    // playback state
    uint32_t play_gen_ = 0;
    bool track_loading_ = false;
    std::string play_err_;
    float vol_ = 0.8f, seek_drag_v_ = 0;
    bool seek_dragging_ = false;
    int shown_sec_ = -1;
    uint32_t last_tick_ = 0;
    std::string scrobbled_id_;
    uint32_t queue_saved_rev_ = 0;
};
