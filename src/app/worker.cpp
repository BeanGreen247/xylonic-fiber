#include "worker.h"
#include <SDL.h>

Worker::Worker(int n) {
    for (int i = 0; i < n; ++i) th_.emplace_back([this] { loop(); });
}

Worker::~Worker() {
    { std::lock_guard<std::mutex> lk(m_); stop_ = true; }
    cv_.notify_all();
    for (auto& t : th_) t.join();
}

void Worker::post(std::function<void()> job) {
    { std::lock_guard<std::mutex> lk(m_); q_.push_back(std::move(job)); }
    cv_.notify_one();
}

void Worker::loop() {
    for (;;) {
        std::function<void()> j;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this] { return stop_ || !q_.empty(); });
            if (stop_ && q_.empty()) return;
            j = std::move(q_.front());
            q_.pop_front();
        }
        j();
    }
}

namespace {
std::mutex g_m;
std::vector<std::function<void()>> g_q;
Uint32 g_event = 0;
}  // namespace

void ui_post(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(g_m);
        g_q.push_back(std::move(fn));
    }
    if (g_event == 0) g_event = SDL_RegisterEvents(1);  // first call is from the UI thread (App constructor)
    SDL_Event e{};
    e.type = g_event;
    SDL_PushEvent(&e);
}

int ui_drain() {
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lk(g_m);
        local.swap(g_q);
    }
    for (auto& f : local) f();
    return static_cast<int>(local.size());
}
