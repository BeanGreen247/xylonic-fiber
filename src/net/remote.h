// LAN remote control (ipc/remote.js): UDP presence broadcast/listen on 7766 (+ multicast 239.255.85.89), HTTP command
// server on 7767 (POST /cmd {action, data, controllerId}). Wire-compatible with the old app so devices interoperate.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct RemoteDevice {
    std::string id, name, host, platform, paired_with, controlling_id, account_id;
    int cmd_port = 7767;
    int64_t last_seen_ms = 0;
    // last reported player state (from presence packets)
    bool playing = false;
    double current_time = 0, duration = 0;
    std::string song_json;  // raw JSON object of the song, empty if none
};

struct RemotePlayerState {  // what this device broadcasts about itself
    bool has_state = false, playing = false;
    double current_time = 0, duration = 0;
    std::string song_json;  // JSON object {"id","title","artist","album","coverArt","duration"} or empty
};

class RemoteService {
public:
    struct Options { int broadcast_port = 7766, command_port = 7767; bool multicast = true, broadcast = true; };
    // Called from the command server thread. Return value is ignored (reply is {"ok":true}).
    using CommandHandler = std::function<void(const std::string& action, const std::string& data_json)>;
    using StateProvider = std::function<RemotePlayerState()>;
    using EventHandler = std::function<void(const std::string& event, const std::string& arg)>;  // pairing-established/cleared, device-found/lost

    RemoteService(std::string device_id, std::string device_name, Options opt);
    ~RemoteService();
    void set_handlers(CommandHandler cmd, StateProvider state, EventHandler ev);
    bool start();
    void stop();
    void set_enabled(bool on);                 // "Be Controlled"
    void set_account_id(const std::string& id) { std::lock_guard<std::mutex> lk(m_); account_ = id; }
    void set_controller_target(const std::string& id) { std::lock_guard<std::mutex> lk(m_); controlling_ = id; }
    std::vector<RemoteDevice> devices();
    std::string paired_controller();
    // Controller side: POST /cmd to a device. Returns the parsed reply: ok flag and reason.
    static bool send_command(const std::string& host, int port, const std::string& action, const std::string& data_json,
                             const std::string& controller_id, std::string& reason);
    const std::string& device_id() const { return id_; }

private:
    void broadcast_loop();
    void listen_loop();
    void server_loop();
    void handle_client(int fd);
    std::string presence_json();
    std::string id_, name_;
    Options opt_;
    std::mutex m_;
    std::string account_, controlling_, paired_controller_;
    bool enabled_ = true;
    std::map<std::string, RemoteDevice> devices_;
    CommandHandler on_cmd_;
    StateProvider state_;
    EventHandler on_event_;
    std::atomic<bool> run_{false};
    std::atomic<int> clients_{0};   // in-flight handle_client threads: capped, and stop() waits for them (they use `this`)
    std::thread th_b_, th_l_, th_s_;
    int lfd_ = -1, sfd_ = -1;
};

std::string local_device_name();
