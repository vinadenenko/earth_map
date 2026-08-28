/**
 * @file ecef_render_frame.h
 * @brief Private camera-relative WGS84/ECEF render coordinate frame.
 *
 * This is the replacement coordinate contract for the normalized-sphere
 * renderer. CPU systems retain double-precision ECEF metres; GPU-facing
 * systems later receive floats relative to this frame's local ENU origin.
 */

#pragma once

#include <earth_map/geodesy/wgs84_ellipsoid.h>

#include <utility>

namespace earth_map::renderer {

class EcefRenderFrame final {
public:
    [[nodiscard]] static EcefRenderFrame FromCamera(
        const geodesy::GeodeticPosition& camera_position) noexcept;

    [[nodiscard]] const geodesy::EcefPosition& CameraOrigin() const noexcept {
        return enu_frame_.origin;
    }

    /** Converts global ECEF metres to double-precision local ENU metres. */
    [[nodiscard]] glm::dvec3 ToLocal(
        const geodesy::EcefPosition& ecef_position) const noexcept;

    /** Converts local ENU metres back to global ECEF metres. */
    [[nodiscard]] geodesy::EcefPosition FromLocal(
        const glm::dvec3& local_meters) const noexcept;

private:
    explicit EcefRenderFrame(geodesy::EnuFrame enu_frame) noexcept
        : enu_frame_(std::move(enu_frame)) {}

    geodesy::EnuFrame enu_frame_;
};

}  // namespace earth_map::renderer
