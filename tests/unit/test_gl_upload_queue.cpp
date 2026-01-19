#include <gtest/gtest.h>
#include <earth_map/renderer/texture_atlas/gl_upload_queue.h>
#include <earth_map/math/tile_mathematics.h>
#include <thread>
#include <vector>
#include <atomic>

namespace earth_map::tests {

/**
 * @brief Test fixture for GLUploadQueue
 */
class GLUploadQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        queue_ = std::make_unique<GLUploadQueue>();
    }

    void TearDown() override {
        queue_.reset();
    }

    /**
     * @brief Create a sample upload command
     */
    std::unique_ptr<GLUploadCommand> CreateTestCommand(
        int32_t x, int32_t y, int32_t zoom,
        std::uint32_t width = 256, std::uint32_t height = 256) {

        auto cmd = std::make_unique<GLUploadCommand>();
        cmd->coords = TileCoordinates(x, y, zoom);
        cmd->width = width;
        cmd->height = height;
        cmd->channels = 3;

        // Create dummy pixel data (RGB)
        const std::size_t data_size = width * height * cmd->channels;
        cmd->pixel_data.resize(data_size);

        // Fill with test pattern
        for (std::size_t i = 0; i < data_size; ++i) {
            cmd->pixel_data[i] = static_cast<std::uint8_t>(i % 256);
        }

        return cmd;
    }

    std::unique_ptr<GLUploadQueue> queue_;
};

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_F(GLUploadQueueTest, InitiallyEmpty) {
    EXPECT_EQ(queue_->Size(), 0u);
}

TEST_F(GLUploadQueueTest, PushSingleCommand) {
    auto cmd = CreateTestCommand(0, 0, 0);
    queue_->Push(std::move(cmd));

    EXPECT_EQ(queue_->Size(), 1u);
}

TEST_F(GLUploadQueueTest, PushAndPopSingleCommand) {
    auto cmd = CreateTestCommand(1, 2, 3);
    const TileCoordinates original_coords = cmd->coords;

    queue_->Push(std::move(cmd));
    ASSERT_EQ(queue_->Size(), 1u);

    auto popped = queue_->TryPop();
    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(popped->coords, original_coords);
    EXPECT_EQ(queue_->Size(), 0u);
}

TEST_F(GLUploadQueueTest, PopFromEmptyQueue) {
    auto cmd = queue_->TryPop();
    EXPECT_EQ(cmd, nullptr);
}

TEST_F(GLUploadQueueTest, FIFOOrdering) {
    // Push multiple commands
    std::vector<TileCoordinates> expected_order;
    for (int i = 0; i < 5; ++i) {
        auto cmd = CreateTestCommand(i, i, i);
        expected_order.push_back(cmd->coords);
        queue_->Push(std::move(cmd));
    }

    // Pop and verify FIFO order
    for (const auto& expected_coords : expected_order) {
        auto popped = queue_->TryPop();
        ASSERT_NE(popped, nullptr);
        EXPECT_EQ(popped->coords, expected_coords);
    }

    EXPECT_EQ(queue_->Size(), 0u);
}

TEST_F(GLUploadQueueTest, DataIntegrity) {
    const std::uint32_t width = 256;
    const std::uint32_t height = 256;
    const std::uint8_t channels = 4; // RGBA

    auto cmd = std::make_unique<GLUploadCommand>();
    cmd->coords = TileCoordinates(5, 10, 8);
    cmd->width = width;
    cmd->height = height;
    cmd->channels = channels;

    // Create test pattern
    const std::size_t data_size = width * height * channels;
    cmd->pixel_data.resize(data_size);
    for (std::size_t i = 0; i < data_size; ++i) {
        cmd->pixel_data[i] = static_cast<std::uint8_t>((i * 13) % 256);
    }

    // Save copy for verification
    std::vector<std::uint8_t> original_data = cmd->pixel_data;

    queue_->Push(std::move(cmd));
    auto popped = queue_->TryPop();

    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(popped->width, width);
    EXPECT_EQ(popped->height, height);
    EXPECT_EQ(popped->channels, channels);
    EXPECT_EQ(popped->pixel_data.size(), data_size);
    EXPECT_EQ(popped->pixel_data, original_data);
}

TEST_F(GLUploadQueueTest, CallbackExecution) {
    std::atomic<bool> callback_called{false};
    TileCoordinates callback_coords;

    auto cmd = CreateTestCommand(7, 8, 9);
    const TileCoordinates expected_coords = cmd->coords;

    cmd->on_complete = [&](const TileCoordinates& coords) {
        callback_called.store(true);
        callback_coords = coords;
    };

    queue_->Push(std::move(cmd));
    auto popped = queue_->TryPop();

    ASSERT_NE(popped, nullptr);

    // Execute callback
    if (popped->on_complete) {
        popped->on_complete(popped->coords);
    }

    EXPECT_TRUE(callback_called.load());
    EXPECT_EQ(callback_coords, expected_coords);
}

// ============================================================================
// Multi-Threading Tests
// ============================================================================

TEST_F(GLUploadQueueTest, ConcurrentPushFromMultipleThreads) {
    constexpr int num_threads = 4;
    constexpr int commands_per_thread = 100;

    std::vector<std::thread> threads;

    // Each thread pushes commands
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < commands_per_thread; ++i) {
                auto cmd = CreateTestCommand(t, i, 0);
                queue_->Push(std::move(cmd));
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify total count
    EXPECT_EQ(queue_->Size(), num_threads * commands_per_thread);
}

TEST_F(GLUploadQueueTest, ConcurrentPushAndPop) {
    constexpr int num_producers = 2;
    constexpr int commands_per_producer = 200;

    std::atomic<int> push_count{0};
    std::atomic<int> pop_count{0};
    std::atomic<bool> stop_flag{false};

    std::vector<std::thread> threads;

    // Producer threads
    for (int t = 0; t < num_producers; ++t) {
        threads.emplace_back([this, t, &push_count]() {
            for (int i = 0; i < commands_per_producer; ++i) {
                auto cmd = CreateTestCommand(t, i, 0);
                queue_->Push(std::move(cmd));
                push_count.fetch_add(1);

                // Small delay to simulate real work
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Consumer thread
    threads.emplace_back([this, &pop_count, &stop_flag]() {
        while (!stop_flag.load() || queue_->Size() > 0) {
            auto cmd = queue_->TryPop();
            if (cmd) {
                pop_count.fetch_add(1);
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });

    // Wait for producers
    for (int i = 0; i < num_producers; ++i) {
        threads[i].join();
    }

    // Signal consumer to stop after queue is empty
    stop_flag.store(true);
    threads.back().join();

    // Verify all commands were processed
    EXPECT_EQ(push_count.load(), num_producers * commands_per_producer);
    EXPECT_EQ(pop_count.load(), push_count.load());
    EXPECT_EQ(queue_->Size(), 0u);
}

TEST_F(GLUploadQueueTest, ThreadSafety_NoDataCorruption) {
    constexpr int num_threads = 8;
    constexpr int commands_per_thread = 50;

    std::vector<std::thread> producer_threads;
    std::vector<std::unique_ptr<GLUploadCommand>> consumed_commands;
    std::mutex consumed_mutex;

    // Producers
    for (int t = 0; t < num_threads; ++t) {
        producer_threads.emplace_back([this, t]() {
            for (int i = 0; i < commands_per_thread; ++i) {
                // Use zoom 8 to allow coordinates up to 255
                auto cmd = CreateTestCommand(t, i, 8);
                queue_->Push(std::move(cmd));
            }
        });
    }

    // Consumer
    std::thread consumer_thread([this, &consumed_commands, &consumed_mutex]() {
        const int total_expected = num_threads * commands_per_thread;
        int consumed = 0;

        while (consumed < total_expected) {
            auto cmd = queue_->TryPop();
            if (cmd) {
                std::lock_guard<std::mutex> lock(consumed_mutex);
                consumed_commands.push_back(std::move(cmd));
                ++consumed;
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
    });

    // Wait for all
    for (auto& thread : producer_threads) {
        thread.join();
    }
    consumer_thread.join();

    // Verify all commands consumed
    EXPECT_EQ(consumed_commands.size(), num_threads * commands_per_thread);

    // Verify no data corruption (all pixel data intact)
    for (const auto& cmd : consumed_commands) {
        ASSERT_NE(cmd, nullptr);
        EXPECT_TRUE(cmd->coords.IsValid());
        EXPECT_EQ(cmd->pixel_data.size(), cmd->width * cmd->height * cmd->channels);
    }
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST_F(GLUploadQueueTest, LargeCommandPayload) {
    // Test with large image data (4K texture)
    constexpr std::uint32_t width = 4096;
    constexpr std::uint32_t height = 4096;
    constexpr std::uint8_t channels = 4;

    auto cmd = std::make_unique<GLUploadCommand>();
    cmd->coords = TileCoordinates(0, 0, 10);
    cmd->width = width;
    cmd->height = height;
    cmd->channels = channels;

    const std::size_t data_size = width * height * channels;
    cmd->pixel_data.resize(data_size);

    // Fill with pattern
    for (std::size_t i = 0; i < data_size; ++i) {
        cmd->pixel_data[i] = static_cast<std::uint8_t>(i % 256);
    }

    queue_->Push(std::move(cmd));
    EXPECT_EQ(queue_->Size(), 1u);

    auto popped = queue_->TryPop();
    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(popped->pixel_data.size(), data_size);
}

TEST_F(GLUploadQueueTest, ManySmallCommands) {
    constexpr int num_commands = 10000;

    for (int i = 0; i < num_commands; ++i) {
        auto cmd = CreateTestCommand(i % 100, i % 100, i % 10);
        queue_->Push(std::move(cmd));
    }

    EXPECT_EQ(queue_->Size(), num_commands);

    // Drain queue
    int popped_count = 0;
    while (auto cmd = queue_->TryPop()) {
        ++popped_count;
    }

    EXPECT_EQ(popped_count, num_commands);
    EXPECT_EQ(queue_->Size(), 0u);
}

} // namespace earth_map::tests
