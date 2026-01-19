/**
 * @file gl_upload_queue.cpp
 * @brief Implementation of thread-safe GL upload queue
 */

#include <earth_map/renderer/texture_atlas/gl_upload_queue.h>
#include <utility>

namespace earth_map {

void GLUploadQueue::Push(std::unique_ptr<GLUploadCommand> cmd) {
    if (!cmd) {
        return; // Ignore null commands
    }

    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(cmd));
}

std::unique_ptr<GLUploadCommand> GLUploadQueue::TryPop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (queue_.empty()) {
        return nullptr;
    }

    // Pop from front (FIFO)
    auto cmd = std::move(queue_.front());
    queue_.pop_front();

    return cmd;
}

std::size_t GLUploadQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

} // namespace earth_map
