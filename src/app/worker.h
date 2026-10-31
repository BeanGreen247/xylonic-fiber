// Background job pool plus a thread-safe mailbox that runs closures on the UI thread.
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class Worker {
public:
    explicit Worker(int threads);
    ~Worker();
    void post(std::function<void()> job);

private:
    void loop();
    std::vector<std::thread> th_;
    std::deque<std::function<void()>> q_;
    std::mutex m_;
    std::condition_variable cv_;
    bool stop_ = false;
};

// Queues fn for the UI thread and wakes its event loop.
void ui_post(std::function<void()> fn);
// Runs everything queued so far; call from the UI thread. Returns the number of closures run.
int ui_drain();
