#pragma once

#include <atomic>
#include <deque>
#include <unordered_set>
#include <functional>

namespace TalosAP {

/// FIFO work queue that executes at most one item per frame.
///
/// Subsystems enqueue work tagged with an integer id.  If the same tag
/// is already in the queue the enqueue is silently skipped, preventing
/// duplicates.  Call ProcessOne() once per frame to drain a single item.
///
/// Usage:
///   // When a throttle fires:
///   m_workQueue.Enqueue(Tag::Visibility, [&]{ DoVisibilityWork(); });
///
///   // Once per frame:
///   m_workQueue.ProcessOne();
class ThrottledWorkQueue {
public:
    using WorkFn = std::function<void()>;

    /// Optionally bind a shutdown flag.  When set, ProcessOne() will
    /// discard work items instead of executing them.
    void SetShutdownFlag(std::atomic<bool>* flag) { m_shuttingDown = flag; }

    /// Enqueue work identified by @p tag.
    /// @returns true if the item was enqueued, false if @p tag is already queued.
    bool Enqueue(int tag, WorkFn fn) {
        if (m_queued.count(tag)) return false;
        m_queue.push_back({ tag, std::move(fn) });
        m_queued.insert(tag);
        return true;
    }

    /// Execute and remove the front item.
    /// @returns true if work was executed, false if the queue was empty.
    bool ProcessOne() {
        if (m_queue.empty()) return false;
        auto entry = std::move(m_queue.front());
        m_queue.pop_front();
        m_queued.erase(entry.tag);
        // Re-check shutdown right before executing — the flag may have
        // been set after Tick()'s top-level guard but before we get here.
        if (m_shuttingDown && m_shuttingDown->load()) return false;
        entry.fn();
        return true;
    }

    /// Check whether a tag is currently queued.
    bool IsQueued(int tag) const { return m_queued.count(tag) != 0; }

    /// Number of items waiting.
    size_t Size() const { return m_queue.size(); }

    /// True when nothing is queued.
    bool Empty() const { return m_queue.empty(); }

    /// Discard all pending work (e.g. on level transition).
    void Clear() {
        m_queue.clear();
        m_queued.clear();
    }

private:
    struct Entry {
        int    tag;
        WorkFn fn;
    };

    std::deque<Entry>       m_queue;
    std::unordered_set<int> m_queued;
    std::atomic<bool>*      m_shuttingDown{nullptr};
};

} // namespace TalosAP
