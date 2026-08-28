/**
 * @file gl_upload_queue.cpp
 * @brief Implementation of thread-safe GL upload queue
 */

#include <earth_map/renderer/texture_atlas/gl_upload_queue.h>
#include <algorithm>
#include <utility>

namespace earth_map {

bool GLUploadQueue::Push(std::unique_ptr<GLUploadCommand> cmd) {
    if (!cmd) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    int priority = 0;
    if (active_filter_enabled_) {
        const auto active = active_priorities_.find(cmd->coords);
        if (active == active_priorities_.end()) {
            return false;
        }
        priority = active->second;
    }

    cmd->enqueued_at = std::chrono::steady_clock::now();
    queue_.push_back({std::move(cmd), priority, next_sequence_++});
    return true;
}

std::unique_ptr<GLUploadCommand> GLUploadQueue::TryPop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.empty()) {
        return nullptr;
    }

    const auto selected = std::min_element(
        queue_.begin(), queue_.end(),
        [](const QueuedCommand& lhs, const QueuedCommand& rhs) {
            return lhs.priority != rhs.priority
                ? lhs.priority < rhs.priority
                : lhs.sequence < rhs.sequence;
        });
    auto cmd = std::move(selected->command);
    queue_.erase(selected);

    return cmd;
}

std::vector<std::unique_ptr<GLUploadCommand>> GLUploadQueue::SetActivePriorities(
    std::unordered_map<TileCoordinates, int, TileCoordinatesHash> priorities) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_priorities_ = std::move(priorities);
    active_filter_enabled_ = true;

    std::deque<QueuedCommand> retained;
    std::vector<std::unique_ptr<GLUploadCommand>> retired;
    for (QueuedCommand& queued : queue_) {
        const auto active = active_priorities_.find(queued.command->coords);
        if (active == active_priorities_.end()) {
            retired.push_back(std::move(queued.command));
            continue;
        }
        queued.priority = active->second;
        retained.push_back(std::move(queued));
    }
    queue_ = std::move(retained);
    return retired;
}

std::size_t GLUploadQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

} // namespace earth_map
