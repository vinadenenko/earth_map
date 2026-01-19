# Texture Atlas Architecture Refactor Plan

**Date:** 2026-01-19
**Scope:** Complete redesign of tile texture management system
**Goal:** Production-ready, scalable, multi-context capable architecture

---

## Design Philosophy

### Core Principle: **Separation of Concerns**

```
CPU-Side (Multi-threaded):          GPU-Side (Single-threaded per context):
┌──────────────────────┐            ┌──────────────────────┐
│  Download Tiles      │            │  Upload to Atlas     │
│  Decode Images       │ ─────────> │  Render with Atlas   │
│  Cache Management    │            │  Atlas Management    │
└──────────────────────┘            └──────────────────────┘
   Many Worker Threads                  GL Thread Only
```

**Key Insight:** Network I/O and image decoding are **CPU-bound, embarrassingly parallel**.
OpenGL operations are **single-threaded per context** and must happen on GL thread.

**Solution:** Separate these concerns completely. Use message passing, not shared state.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Application Layer                            │
│  ┌────────────────────────────────────────────────────────────┐    │
│  │  Visibility Determination (Frustum Culling)                 │    │
│  │    → Generates list of needed TileCoordinates              │    │
│  └────────────────────────────────────────────────────────────┘    │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ RequestTiles(coords[])
                           ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   TileTextureCoordinator                            │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │ • Tile State Tracker (not_loaded, loading, loaded)           │  │
│  │ • Request Deduplication                                      │  │
│  │ • Priority Management                                        │  │
│  │ • UV Coordinate Lookup (from Atlas)                          │  │
│  └──────────────────────────────────────────────────────────────┘  │
└────────────┬────────────────────────────────────────┬───────────────┘
             │                                        │
             │ Dispatch Load Request                  │ Query UV
             ▼                                        ▼
┌──────────────────────────┐              ┌──────────────────────────┐
│  TileLoadWorkerPool      │              │  TextureAtlasManager     │
│  ┌────────────────────┐  │              │  ┌────────────────────┐  │
│  │ Worker Thread 1    │  │              │  │ Atlas Texture (GL) │  │
│  │ Worker Thread 2    │  │              │  │ Slot Allocation    │  │
│  │ Worker Thread 3    │  │              │  │ UV Generation      │  │
│  │ Worker Thread 4    │  │              │  │ Eviction Policy    │  │
│  └────────────────────┘  │              │  └────────────────────┘  │
│                          │              │                          │
│  Each Worker:            │              │  Only accessed from      │
│  1. Fetch from Cache     │              │  GL thread!              │
│  2. Download if missing  │              │                          │
│  3. Decode image         │              └──────────────────────────┘
│  4. Create UploadCommand │                          ▲
│  5. Push to GL Queue     │                          │
└──────────┬───────────────┘                          │
           │                                          │
           │ Push UploadCommand                       │
           ▼                                          │
┌──────────────────────────┐                          │
│  GLUploadQueue           │                          │
│  (Lock-free MPSC Queue)  │                          │
│                          │                          │
│  [Cmd1][Cmd2][Cmd3]...   │ ─────────────────────────┘
└──────────────────────────┘           Drain & Execute
                                       (per frame, budgeted)
```

---

## Component Specifications

### 1. TileTextureCoordinator

**Responsibility:** Coordinate tile loading and provide UV lookups

**Interface:**
```cpp
class TileTextureCoordinator {
public:
    // Request tiles to be loaded (non-blocking)
    void RequestTiles(const std::vector<TileCoordinates>& tiles,
                     int priority = 0);

    // Check if tile texture is ready
    bool IsTileReady(const TileCoordinates& coords) const;

    // Get UV coordinates for tile (returns valid UV or default if not ready)
    glm::vec4 GetTileUV(const TileCoordinates& coords) const;

    // Get atlas texture ID (for binding in renderer)
    std::uint32_t GetAtlasTextureID() const;

    // Called from GL thread per frame to process uploads
    void ProcessUploads(int max_uploads_per_frame = 5);

    // Evict old tiles to free space
    void EvictUnusedTiles(std::chrono::seconds max_age);

private:
    // Tile state tracking (lock-free or minimal locking)
    struct TileState {
        enum class Status { NotLoaded, Loading, Loaded };
        Status status;
        std::uint32_t atlas_slot;  // Only valid if Loaded
    };

    std::unordered_map<TileCoordinates, TileState> tile_states_;
    std::shared_mutex state_mutex_;  // Read-write lock

    // Worker pool for loading
    std::unique_ptr<TileLoadWorkerPool> worker_pool_;

    // GL upload queue
    std::shared_ptr<GLUploadQueue> upload_queue_;

    // Atlas manager (GL thread only)
    std::unique_ptr<TextureAtlasManager> atlas_manager_;
};
```

**Key Design Points:**
- Uses **read-write lock** (`std::shared_mutex`) for tile_states_
  - Many threads can query status concurrently
  - Only state transitions need exclusive lock
- **No GL calls** in this class except in `ProcessUploads()`
- Worker pool handles all CPU-bound work

---

### 2. TileLoadWorkerPool

**Responsibility:** Manage worker threads for tile loading

**Interface:**
```cpp
class TileLoadWorkerPool {
public:
    explicit TileLoadWorkerPool(
        std::shared_ptr<TileCache> cache,
        std::shared_ptr<TileLoader> loader,
        std::shared_ptr<GLUploadQueue> upload_queue,
        int num_threads = 4);

    ~TileLoadWorkerPool();

    // Submit tile load request
    void SubmitRequest(const TileLoadRequest& request);

    // Shutdown worker threads
    void Shutdown();

private:
    struct TileLoadRequest {
        TileCoordinates coords;
        int priority;
        std::function<void(const TileCoordinates&)> on_complete;
    };

    // Worker thread function
    void WorkerThreadMain();

    // Components
    std::shared_ptr<TileCache> cache_;
    std::shared_ptr<TileLoader> loader_;
    std::shared_ptr<GLUploadQueue> upload_queue_;

    // Threading
    std::vector<std::thread> workers_;
    std::atomic<bool> shutdown_flag_{false};

    // Request queue (thread-safe priority queue)
    ConcurrentPriorityQueue<TileLoadRequest> request_queue_;
};
```

**Worker Thread Logic:**
```cpp
void WorkerThreadMain() {
    while (!shutdown_flag_) {
        auto request = request_queue_.Pop();  // Blocking wait

        // 1. Check cache
        auto cached_data = cache_->Retrieve(request.coords);

        // 2. Download if not cached
        if (!cached_data || !cached_data->IsValid()) {
            auto result = loader_->LoadTile(request.coords);  // Synchronous on worker thread
            if (result.success) {
                cached_data = result.tile_data;
                cache_->Store(request.coords, *cached_data);
            } else {
                continue;  // Failed to load
            }
        }

        // 3. Decode image
        DecodedImage image = DecodeImage(cached_data->data);
        if (!image.IsValid()) {
            continue;
        }

        // 4. Create upload command
        auto upload_cmd = std::make_unique<GLUploadCommand>();
        upload_cmd->coords = request.coords;
        upload_cmd->pixel_data = std::move(image.pixel_data);
        upload_cmd->width = image.width;
        upload_cmd->height = image.height;
        upload_cmd->channels = image.channels;
        upload_cmd->on_complete = request.on_complete;

        // 5. Push to GL upload queue
        upload_queue_->Push(std::move(upload_cmd));
    }
}
```

---

### 3. GLUploadQueue

**Responsibility:** Thread-safe queue of upload commands for GL thread

**Interface:**
```cpp
struct GLUploadCommand {
    TileCoordinates coords;
    std::vector<std::uint8_t> pixel_data;
    std::uint32_t width;
    std::uint32_t height;
    std::uint8_t channels;
    std::function<void(const TileCoordinates&)> on_complete;
};

class GLUploadQueue {
public:
    // Push command (called from worker threads)
    void Push(std::unique_ptr<GLUploadCommand> cmd);

    // Pop command (called from GL thread)
    std::unique_ptr<GLUploadCommand> TryPop();

    // Get queue size
    std::size_t Size() const;

private:
    // Lock-free MPSC (multi-producer, single-consumer) queue
    moodycamel::ConcurrentQueue<std::unique_ptr<GLUploadCommand>> queue_;
};
```

**Why Lock-Free Queue:**
- Multiple worker threads push (producers)
- Single GL thread pops (consumer)
- **moodycamel::ConcurrentQueue** is ideal for this pattern
- Zero contention, no mutexes, blazing fast

**Alternative (if no external lib):**
```cpp
// Simple mutex-based version
class GLUploadQueue {
    std::mutex mutex_;
    std::deque<std::unique_ptr<GLUploadCommand>> queue_;

    void Push(std::unique_ptr<GLUploadCommand> cmd) {
        std::lock_guard lock(mutex_);
        queue_.push_back(std::move(cmd));
    }

    std::unique_ptr<GLUploadCommand> TryPop() {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) return nullptr;
        auto cmd = std::move(queue_.front());
        queue_.pop_front();
        return cmd;
    }
};
```

---

### 4. TextureAtlasManager

**Responsibility:** Manage OpenGL atlas texture and tile slots

**Interface:**
```cpp
class TextureAtlasManager {
public:
    explicit TextureAtlasManager(const AtlasConfig& config);

    // Upload tile to atlas (returns slot index, or -1 if full)
    int UploadTile(const TileCoordinates& coords,
                   const std::uint8_t* pixel_data,
                   std::uint32_t width, std::uint32_t height,
                   std::uint8_t channels);

    // Get UV coordinates for tile
    glm::vec4 GetTileUV(const TileCoordinates& coords) const;

    // Get atlas OpenGL texture ID
    std::uint32_t GetTextureID() const { return atlas_texture_id_; }

    // Evict tile from atlas
    void EvictTile(const TileCoordinates& coords);

    // Get atlas statistics
    AtlasStats GetStats() const;

private:
    struct AtlasSlot {
        TileCoordinates coords;
        bool occupied = false;
        std::chrono::steady_clock::time_point last_used;

        // UV coordinates (cached)
        glm::vec4 uv_coords;  // (u_min, v_min, u_max, v_max)
    };

    // Atlas configuration
    AtlasConfig config_;
    std::uint32_t atlas_texture_id_ = 0;

    // Slot management
    std::vector<AtlasSlot> slots_;
    std::queue<int> free_slots_;
    std::unordered_map<TileCoordinates, int> coord_to_slot_;

    // Internal methods
    glm::vec4 CalculateUV(int slot_index) const;
    int FindEvictionCandidate() const;  // LRU policy
    void CreateAtlasTexture();
};
```

**Atlas Layout:**
```
Atlas Size: 2048x2048
Tile Size: 256x256
Grid: 8x8 = 64 slots

┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
│ 0,0 │ 1,0 │ 2,0 │ 3,0 │ 4,0 │ 5,0 │ 6,0 │ 7,0 │
├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│ 0,1 │ 1,1 │ 2,1 │ 3,1 │ 4,1 │ 5,1 │ 6,1 │ 7,1 │
├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
│ ... │ ... │ ... │ ... │ ... │ ... │ ... │ ... │
└─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘

Slot 0 UV: (0.0, 0.0, 0.125, 0.125)
Slot 1 UV: (0.125, 0.0, 0.25, 0.125)
...
```

**UV Calculation:**
```cpp
glm::vec4 CalculateUV(int slot_index) const {
    int tiles_per_row = config_.atlas_size / config_.tile_size;
    int row = slot_index / tiles_per_row;
    int col = slot_index % tiles_per_row;

    float tile_uv_size = 1.0f / tiles_per_row;

    float u_min = col * tile_uv_size;
    float v_min = row * tile_uv_size;
    float u_max = u_min + tile_uv_size;
    float v_max = v_min + tile_uv_size;

    return glm::vec4(u_min, v_min, u_max, v_max);
}
```

---

### 5. Shader Integration

**Updated Fragment Shader:**
```glsl
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D uAtlasTexture;
uniform vec3 uLightPos;
uniform vec3 uLightColor;

// Tile UV lookup (passed from CPU)
// We'll use a uniform buffer or texture for mapping coords → UV
// For now, calculate dynamically from geographic position

vec3 worldToGeo(vec3 worldPos) {
    float lat = degrees(asin(clamp(worldPos.y, -1.0, 1.0)));
    float lon = degrees(atan(worldPos.x, worldPos.z));
    return vec3(lon, lat, 0.0);
}

vec2 geoToTileCoords(vec2 geo, float zoom) {
    float n = pow(2.0, zoom);
    float x = (geo.x + 180.0) / 360.0 * n;
    float lat_rad = radians(clamp(geo.y, -85.0511, 85.0511));
    float y = (1.0 - log(tan(lat_rad) + 1.0/cos(lat_rad)) / 3.14159265359) / 2.0 * n;
    return vec2(x, y);
}

void main() {
    // Lighting
    float ambientStrength = 0.25;
    vec3 ambient = ambientStrength * uLightColor;

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(uLightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;

    // Get geographic coordinates from world position
    vec3 geo = worldToGeo(normalize(FragPos));

    // Calculate tile coordinates (use appropriate zoom level)
    float zoom = 2.0;  // TODO: Pass as uniform or calculate from camera distance
    vec2 tileCoords = geoToTileCoords(geo.xy, zoom);

    // Get fractional position within tile
    vec2 tileFrac = fract(tileCoords);

    // TODO: In production, use a lookup texture or UBO to map tile coords to atlas UV
    // For now, assume simple atlas layout
    int tilesPerRow = 8;  // 2048 / 256
    vec2 tileInt = floor(tileCoords);
    vec2 atlasPos = mod(tileInt, vec2(tilesPerRow));
    float tileUVSize = 1.0 / float(tilesPerRow);
    vec2 atlasUV = (atlasPos + tileFrac) * tileUVSize;

    // Sample from atlas
    vec4 texColor = texture(uAtlasTexture, atlasUV);

    // Apply lighting
    vec3 result = (ambient + diffuse) * texColor.rgb;
    FragColor = vec4(result, texColor.a);
}
```

**Better Approach: Tile Index Texture**

Instead of calculating in shader, use a **tile index texture**:
- 256x256 texture storing atlas slot indices
- Each pixel represents which atlas slot to use
- Shader does simple lookup + UV calculation

---

## Data Flow Example

### Frame N: Request Tiles

```cpp
// In TileRenderer::UpdateVisibleTiles()
std::vector<TileCoordinates> visible_tiles = CalculateVisibleTiles(frustum);

coordinator_->RequestTiles(visible_tiles, priority);
```

**Coordinator logic:**
```cpp
void RequestTiles(const std::vector<TileCoordinates>& tiles, int priority) {
    std::vector<TileCoordinates> tiles_to_load;

    {
        std::shared_lock lock(state_mutex_);  // Read lock
        for (const auto& coords : tiles) {
            auto it = tile_states_.find(coords);
            if (it == tile_states_.end() || it->second.status == Status::NotLoaded) {
                tiles_to_load.push_back(coords);
            }
        }
    }

    if (!tiles_to_load.empty()) {
        std::unique_lock lock(state_mutex_);  // Write lock
        for (const auto& coords : tiles_to_load) {
            tile_states_[coords].status = Status::Loading;

            TileLoadRequest request;
            request.coords = coords;
            request.priority = priority;
            request.on_complete = [this](const TileCoordinates& coords) {
                OnTileLoaded(coords);
            };

            worker_pool_->SubmitRequest(request);
        }
    }
}
```

### Background: Worker Loads Tile

```cpp
// In worker thread
TileLoadRequest req = request_queue_.Pop();

// Load from cache/network
auto data = LoadTileData(req.coords);

// Decode image
auto image = DecodeImage(data);

// Create upload command
auto cmd = std::make_unique<GLUploadCommand>();
cmd->coords = req.coords;
cmd->pixel_data = std::move(image.pixel_data);
cmd->width = image.width;
cmd->height = image.height;
cmd->on_complete = req.on_complete;

// Push to GL queue
upload_queue_->Push(std::move(cmd));
```

### Frame N+k: Process Uploads

```cpp
// In TileRenderer::BeginFrame() or Render()
coordinator_->ProcessUploads(5);  // Budget: 5 uploads per frame
```

**ProcessUploads logic:**
```cpp
void ProcessUploads(int max_uploads) {
    int uploads_done = 0;

    while (uploads_done < max_uploads) {
        auto cmd = upload_queue_->TryPop();
        if (!cmd) break;  // Queue empty

        // Upload to atlas (GL calls happen here)
        int slot = atlas_manager_->UploadTile(
            cmd->coords,
            cmd->pixel_data.data(),
            cmd->width,
            cmd->height,
            cmd->channels
        );

        if (slot >= 0) {
            // Update tile state
            {
                std::unique_lock lock(state_mutex_);
                tile_states_[cmd->coords].status = Status::Loaded;
                tile_states_[cmd->coords].atlas_slot = slot;
            }

            // Notify completion
            if (cmd->on_complete) {
                cmd->on_complete(cmd->coords);
            }
        }

        uploads_done++;
    }
}
```

### Frame N+k: Render

```cpp
// Get atlas texture
GLuint atlas_texture = coordinator_->GetAtlasTextureID();

// Bind atlas
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_2D, atlas_texture);

// Render globe mesh
glUseProgram(shader_program);
glUniform1i(atlas_texture_loc, 0);
glDrawElements(GL_TRIANGLES, ...);
```

---

## Migration Strategy

### Phase 1: Implement New Components (Parallel Development)

**Week 1:**
1. Implement `GLUploadQueue` (lock-free or mutex-based)
2. Implement `GLUploadCommand` structure
3. Write unit tests

**Week 2:**
4. Implement `TextureAtlasManager`
5. Test atlas slot allocation, UV calculation
6. Test eviction policy

**Week 3:**
7. Implement `TileLoadWorkerPool`
8. Test worker thread logic
9. Integration test with mock GL queue

**Week 4:**
10. Implement `TileTextureCoordinator`
11. Wire all components together
12. Integration tests

### Phase 2: Adapt Existing Code

**Week 5:**
1. Update `TileRenderer` to use `TileTextureCoordinator`
2. Remove old `TileTextureManager` calls
3. Update shaders for atlas usage

**Week 6:**
4. Update `TileManager` integration
5. Remove old texture manager
6. Migration testing

### Phase 3: Validation & Optimization

**Week 7:**
1. Performance profiling
2. Memory leak testing
3. Stress testing (1000s of tile requests)

**Week 8:**
4. Documentation
5. API finalization
6. Release candidate

---

## File Structure

```
include/earth_map/renderer/
├── texture_atlas/
│   ├── tile_texture_coordinator.h
│   ├── tile_load_worker_pool.h
│   ├── gl_upload_queue.h
│   ├── texture_atlas_manager.h
│   └── atlas_types.h

src/renderer/texture_atlas/
├── tile_texture_coordinator.cpp
├── tile_load_worker_pool.cpp
├── gl_upload_queue.cpp
└── texture_atlas_manager.cpp
```

---

## Benefits of New Architecture

### 1. **Clean Separation**
- CPU work (download, decode) fully separated from GPU work (upload)
- No GL calls on worker threads
- No network I/O on GL thread

### 2. **Scalability**
- Worker pool scales to N threads
- Upload budget prevents frame drops
- Easy to add more atlases if needed

### 3. **Thread Safety**
- Message passing eliminates most locking
- Read-write lock for tile state lookup
- Lock-free upload queue (zero contention)

### 4. **Testability**
- Each component independently testable
- Mock upload queue for worker tests
- Mock atlas for coordinator tests

### 5. **Debuggability**
- Clear data flow: Request → Load → Upload → Render
- Statistics at each stage
- Easy to log/trace tile lifecycle

### 6. **Future-Proof**
- Multiple atlases: trivial to add
- Multiple GL contexts: one coordinator + queue per context
- Texture compression: add compression step in worker
- Prefetching: add prediction logic to coordinator

---

## Performance Characteristics

### Upload Budget

**Without budget:**
- 100 tiles arrive → 100 uploads → frame drops to 10 FPS

**With budget (5 uploads/frame @ 60 FPS):**
- 100 tiles arrive → 5 uploads/frame → 20 frames to complete → 333ms total
- Smooth 60 FPS throughout

### Memory Usage

**Current (broken):**
- Tiles loaded individually → 1000 tiles × 256x256x4 = 250 MB

**Atlas (optimized):**
- 4 atlases × 2048x2048x4 = 64 MB
- 4 × 64 slots = 256 active tiles max
- Automatic eviction for older tiles

### Latency

**Tile request → visible on screen:**
- Network latency: 50-200ms (depends on server)
- Decode: 5-10ms (CPU)
- Upload wait: 0-100ms (depends on queue depth)
- **Total: 55-310ms** (acceptable for user experience)

---

## Testing Strategy

### Unit Tests

1. **GLUploadQueue**
   - Push from multiple threads
   - Pop from single thread
   - Verify no data corruption

2. **TextureAtlasManager**
   - Slot allocation
   - UV calculation accuracy
   - Eviction policy (LRU)
   - Atlas texture creation

3. **TileLoadWorkerPool**
   - Request queue ordering (priority)
   - Worker thread lifecycle
   - Shutdown behavior

4. **TileTextureCoordinator**
   - Duplicate request handling
   - State transitions
   - UV lookup correctness

### Integration Tests

1. **Full pipeline test**
   - Request 100 tiles
   - Verify all loaded
   - Check atlas slots allocated
   - Verify UV coordinates

2. **Stress test**
   - Request 10,000 tiles rapidly
   - Verify bounded memory
   - Verify eviction works
   - Check for deadlocks/crashes

3. **Rendering test**
   - Visual verification of atlas rendering
   - Check for UV seams
   - Verify tile updates appear

---

## Success Criteria

✅ No deadlocks or race conditions
✅ Smooth 60 FPS with continuous tile loading
✅ Memory usage bounded and predictable
✅ Zero GL calls on non-GL threads
✅ Clean shutdown with no leaks
✅ >80% test coverage
✅ Supports 1000+ tile requests/second
✅ Atlas rendering is seamless (no visible seams/gaps)

---

## Next Steps

1. **Review this plan** - validate design decisions
2. **Prototype GLUploadQueue** - prove lock-free queue works
3. **Prototype TextureAtlasManager** - prove atlas approach works
4. **Implement Phase 1** - build components in isolation
5. **Integration** - wire everything together
6. **Validation** - extensive testing

---

## Estimated Effort

| Phase | Tasks | Effort |
|-------|-------|--------|
| Phase 1 | New components | 3-4 weeks |
| Phase 2 | Integration | 1-2 weeks |
| Phase 3 | Validation | 1 week |
| **Total** | | **5-7 weeks** |

With proper design upfront, implementation should be straightforward.

This is a **NASA enterprise-level** architecture that will serve the project for years to come.
