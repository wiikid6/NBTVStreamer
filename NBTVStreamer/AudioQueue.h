#pragma once
#include <deque>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>


// Thread-safe FIFO of float audio samples shared between encoder and WASAPI thread.
class AudioQueue
{
public:
    explicit AudioQueue(size_t maxSamples = 48000)   // 10 s default headroom
        : m_maxSamples(maxSamples), m_closed(false) {}

    void Push(const std::vector<float>& samples)
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        // Drop oldest data if we are too far behind.
        while (m_buf.size() + samples.size() > m_maxSamples && !m_buf.empty())
            m_buf.pop_front();
        for (float s : samples) m_buf.push_back(s);
        m_cv.notify_one();
    }

    // Blocking pop: fills out with up to 'count' samples.
    // Returns false when queue is closed and empty.
    bool Pop(float* out, size_t count)
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        m_cv.wait(lk, [&]{ return m_buf.size() >= count || m_closed; });
        if (m_buf.empty()) return false;
        size_t n = min(count, m_buf.size());
        for (size_t i = 0; i < n; ++i) { out[i] = m_buf.front(); m_buf.pop_front(); }
        if (n < count) std::fill(out + n, out + count, 0.0f);   // pad silence
        return true;
    }

    void Close()
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        m_closed = true;
        m_cv.notify_all();
    }

    void Reset()
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        m_buf.clear();
        m_closed = false;
    }

    size_t Size() const
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        return m_buf.size();
    }

private:
    mutable std::mutex          m_mtx;
    std::condition_variable     m_cv;
    std::deque<float>           m_buf;
    size_t                      m_maxSamples;
    bool                        m_closed;
};
