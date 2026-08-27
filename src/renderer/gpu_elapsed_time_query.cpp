/**
 * @file gpu_elapsed_time_query.cpp
 * @brief See gpu_elapsed_time_query.h.
 */

#include "gpu_elapsed_time_query.h"

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#else
#include <GL/glew.h>
#endif

#include <spdlog/spdlog.h>

#include <cstring>

namespace earth_map {

#ifdef __ANDROID__

namespace {

// GL_EXT_disjoint_timer_query entry points aren't statically linked on
// Android (it's a genuine optional extension) -- resolved once via
// eglGetProcAddress on first use.
struct DisjointTimerQueryExt {
    PFNGLGENQUERIESEXTPROC GenQueries = nullptr;
    PFNGLDELETEQUERIESEXTPROC DeleteQueries = nullptr;
    PFNGLBEGINQUERYEXTPROC BeginQuery = nullptr;
    PFNGLENDQUERYEXTPROC EndQuery = nullptr;
    PFNGLGETQUERYOBJECTUIVEXTPROC GetQueryObjectuiv = nullptr;
    PFNGLGETQUERYOBJECTUI64VEXTPROC GetQueryObjectui64v = nullptr;
    bool supported = false;
    bool resolved = false;
};

bool ExtensionStringPresent(const char* name) {
    GLint extension_count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
    for (GLint i = 0; i < extension_count; ++i) {
        const auto* extension =
            reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (extension && std::strcmp(extension, name) == 0) {
            return true;
        }
    }
    return false;
}

const DisjointTimerQueryExt& GetExt() {
    static DisjointTimerQueryExt ext = [] {
        DisjointTimerQueryExt result;
        result.resolved = true;
        if (!ExtensionStringPresent("GL_EXT_disjoint_timer_query")) {
            spdlog::info("GPU timer queries unavailable: GL_EXT_disjoint_timer_query not present");
            return result;
        }

        result.GenQueries = reinterpret_cast<PFNGLGENQUERIESEXTPROC>(
            eglGetProcAddress("glGenQueriesEXT"));
        result.DeleteQueries = reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(
            eglGetProcAddress("glDeleteQueriesEXT"));
        result.BeginQuery = reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(
            eglGetProcAddress("glBeginQueryEXT"));
        result.EndQuery = reinterpret_cast<PFNGLENDQUERYEXTPROC>(
            eglGetProcAddress("glEndQueryEXT"));
        result.GetQueryObjectuiv = reinterpret_cast<PFNGLGETQUERYOBJECTUIVEXTPROC>(
            eglGetProcAddress("glGetQueryObjectuivEXT"));
        result.GetQueryObjectui64v = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(
            eglGetProcAddress("glGetQueryObjectui64vEXT"));

        result.supported = result.GenQueries && result.DeleteQueries && result.BeginQuery &&
                            result.EndQuery && result.GetQueryObjectuiv &&
                            result.GetQueryObjectui64v;
        if (!result.supported) {
            spdlog::warn(
                "GL_EXT_disjoint_timer_query advertised but eglGetProcAddress returned null "
                "for one or more entry points; GPU timer queries disabled");
        }
        return result;
    }();
    return ext;
}

}  // namespace

bool GpuElapsedTimeQuery::IsSupported() { return GetExt().supported; }

GpuElapsedTimeQuery::GpuElapsedTimeQuery() {
    if (!IsSupported()) {
        return;
    }
    GetExt().GenQueries(static_cast<GLsizei>(query_ids_.size()), query_ids_.data());
    initialized_ = true;
}

GpuElapsedTimeQuery::~GpuElapsedTimeQuery() {
    if (initialized_) {
        GetExt().DeleteQueries(static_cast<GLsizei>(query_ids_.size()), query_ids_.data());
    }
}

void GpuElapsedTimeQuery::Begin() {
    if (!initialized_ || active_index_ >= 0) {
        return;
    }
    const auto slot = FindFreeSlot();
    if (!slot) {
        ++diagnostics_.skipped_no_free_slot;
        return;
    }
    active_index_ = static_cast<int>(*slot);
    GetExt().BeginQuery(GL_TIME_ELAPSED_EXT, query_ids_[*slot]);
}

void GpuElapsedTimeQuery::End() {
    if (!initialized_ || active_index_ < 0) {
        return;
    }
    GetExt().EndQuery(GL_TIME_ELAPSED_EXT);
    const std::size_t slot = static_cast<std::size_t>(active_index_);
    query_pending_[slot] = true;
    submission_order_[slot] = next_submission_order_++;
    ++diagnostics_.submitted;
    active_index_ = -1;
}

std::optional<double> GpuElapsedTimeQuery::TryTakePreviousResultMs() {
    if (!initialized_) {
        return std::nullopt;
    }

    const auto slot = FindOldestPendingSlot();
    if (!slot) {
        return std::nullopt;
    }

    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);

    GLuint available = 0;
    GetExt().GetQueryObjectuiv(query_ids_[*slot], GL_QUERY_RESULT_AVAILABLE_EXT, &available);
    if (!available) {
        return std::nullopt;
    }

    query_pending_[*slot] = false;

    GLuint64 elapsed_nanoseconds = 0;
    GetExt().GetQueryObjectui64v(query_ids_[*slot], GL_QUERY_RESULT_EXT, &elapsed_nanoseconds);

    if (disjoint) {
        // GPU clock changed mid-measurement (e.g. thermal throttling) --
        // the result is meaningless.
        ++diagnostics_.discarded_disjoint;
        return std::nullopt;
    }

    ++diagnostics_.resolved;
    return static_cast<double>(elapsed_nanoseconds) / 1'000'000.0;
}

#else  // !__ANDROID__

bool GpuElapsedTimeQuery::IsSupported() { return true; }

GpuElapsedTimeQuery::GpuElapsedTimeQuery() {
    glGenQueries(static_cast<GLsizei>(query_ids_.size()), query_ids_.data());
    initialized_ = true;
}

GpuElapsedTimeQuery::~GpuElapsedTimeQuery() {
    if (initialized_) {
        glDeleteQueries(static_cast<GLsizei>(query_ids_.size()), query_ids_.data());
    }
}

void GpuElapsedTimeQuery::Begin() {
    if (!initialized_ || active_index_ >= 0) {
        return;
    }
    const auto slot = FindFreeSlot();
    if (!slot) {
        ++diagnostics_.skipped_no_free_slot;
        return;
    }
    active_index_ = static_cast<int>(*slot);
    glBeginQuery(GL_TIME_ELAPSED, query_ids_[*slot]);
}

void GpuElapsedTimeQuery::End() {
    if (!initialized_ || active_index_ < 0) {
        return;
    }
    glEndQuery(GL_TIME_ELAPSED);
    const std::size_t slot = static_cast<std::size_t>(active_index_);
    query_pending_[slot] = true;
    submission_order_[slot] = next_submission_order_++;
    ++diagnostics_.submitted;
    active_index_ = -1;
}

std::optional<double> GpuElapsedTimeQuery::TryTakePreviousResultMs() {
    const auto slot = FindOldestPendingSlot();
    if (!slot) {
        return std::nullopt;
    }

    GLint available = 0;
    glGetQueryObjectiv(query_ids_[*slot], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) {
        return std::nullopt;
    }

    query_pending_[*slot] = false;
    ++diagnostics_.resolved;

    GLuint64 elapsed_nanoseconds = 0;
    glGetQueryObjectui64v(query_ids_[*slot], GL_QUERY_RESULT, &elapsed_nanoseconds);
    return static_cast<double>(elapsed_nanoseconds) / 1'000'000.0;
}

#endif  // __ANDROID__

std::optional<std::size_t> GpuElapsedTimeQuery::FindFreeSlot() {
    for (std::size_t offset = 0; offset < kQueryRingSize; ++offset) {
        const std::size_t slot = (next_write_index_ + offset) % kQueryRingSize;
        if (!query_pending_[slot] && static_cast<int>(slot) != active_index_) {
            next_write_index_ = (slot + 1) % kQueryRingSize;
            return slot;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> GpuElapsedTimeQuery::FindOldestPendingSlot() const {
    std::optional<std::size_t> oldest;
    for (std::size_t slot = 0; slot < kQueryRingSize; ++slot) {
        if (!query_pending_[slot]) {
            continue;
        }
        if (!oldest || submission_order_[slot] < submission_order_[*oldest]) {
            oldest = slot;
        }
    }
    return oldest;
}

}  // namespace earth_map
