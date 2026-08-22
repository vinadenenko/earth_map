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
    GetExt().GenQueries(2, query_ids_);
    initialized_ = true;
}

GpuElapsedTimeQuery::~GpuElapsedTimeQuery() {
    if (initialized_) {
        GetExt().DeleteQueries(2, query_ids_);
    }
}

void GpuElapsedTimeQuery::Begin() {
    if (!initialized_) {
        return;
    }
    GetExt().BeginQuery(GL_TIME_ELAPSED_EXT, query_ids_[write_index_]);
}

void GpuElapsedTimeQuery::End() {
    if (!initialized_) {
        return;
    }
    GetExt().EndQuery(GL_TIME_ELAPSED_EXT);
    query_pending_[write_index_] = true;
    write_index_ = 1 - write_index_;
}

std::optional<double> GpuElapsedTimeQuery::TryTakePreviousResultMs() {
    if (!initialized_) {
        return std::nullopt;
    }

    // write_index_ already points at the slot End() just moved *off of*
    // (End() advances it right after marking that slot pending) -- so by
    // the time we get here, write_index_ names the *other* slot: the one
    // left over from the previous frame's End(), not this frame's. No
    // inversion needed; inverting it (as an earlier version of this code
    // did) targets this frame's own just-submitted query instead, which
    // the GPU essentially never finishes in time, and the true previous
    // result is silently orphaned.
    const int read_index = write_index_;
    if (!query_pending_[read_index]) {
        return std::nullopt;
    }

    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);

    GLuint available = 0;
    GetExt().GetQueryObjectuiv(query_ids_[read_index], GL_QUERY_RESULT_AVAILABLE_EXT, &available);
    if (!available) {
        return std::nullopt;
    }

    query_pending_[read_index] = false;

    GLuint64 elapsed_nanoseconds = 0;
    GetExt().GetQueryObjectui64v(query_ids_[read_index], GL_QUERY_RESULT_EXT, &elapsed_nanoseconds);

    if (disjoint) {
        // GPU clock changed mid-measurement (e.g. thermal throttling) --
        // the result is meaningless.
        return std::nullopt;
    }

    return static_cast<double>(elapsed_nanoseconds) / 1'000'000.0;
}

#else  // !__ANDROID__

bool GpuElapsedTimeQuery::IsSupported() { return true; }

GpuElapsedTimeQuery::GpuElapsedTimeQuery() {
    glGenQueries(2, query_ids_);
    initialized_ = true;
}

GpuElapsedTimeQuery::~GpuElapsedTimeQuery() {
    if (initialized_) {
        glDeleteQueries(2, query_ids_);
    }
}

void GpuElapsedTimeQuery::Begin() { glBeginQuery(GL_TIME_ELAPSED, query_ids_[write_index_]); }

void GpuElapsedTimeQuery::End() {
    glEndQuery(GL_TIME_ELAPSED);
    query_pending_[write_index_] = true;
    write_index_ = 1 - write_index_;
}

std::optional<double> GpuElapsedTimeQuery::TryTakePreviousResultMs() {
    // write_index_ already points at the slot End() just moved *off of*
    // (End() advances it right after marking that slot pending) -- so by
    // the time we get here, write_index_ names the *other* slot: the one
    // left over from the previous frame's End(), not this frame's. No
    // inversion needed; inverting it (as an earlier version of this code
    // did) targets this frame's own just-submitted query instead, which
    // the GPU essentially never finishes in time, and the true previous
    // result is silently orphaned.
    const int read_index = write_index_;
    if (!query_pending_[read_index]) {
        return std::nullopt;
    }

    GLint available = 0;
    glGetQueryObjectiv(query_ids_[read_index], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) {
        return std::nullopt;
    }

    query_pending_[read_index] = false;

    GLuint64 elapsed_nanoseconds = 0;
    glGetQueryObjectui64v(query_ids_[read_index], GL_QUERY_RESULT, &elapsed_nanoseconds);
    return static_cast<double>(elapsed_nanoseconds) / 1'000'000.0;
}

#endif  // __ANDROID__

}  // namespace earth_map
