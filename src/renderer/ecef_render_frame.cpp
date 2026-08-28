#include "ecef_render_frame.h"

#include <utility>

namespace earth_map::renderer {

EcefRenderFrame EcefRenderFrame::FromCamera(
    const geodesy::GeodeticPosition& camera_position) noexcept {
    return EcefRenderFrame{geodesy::Wgs84Ellipsoid::MakeEnuFrame(camera_position)};
}

glm::dvec3 EcefRenderFrame::ToLocal(
    const geodesy::EcefPosition& ecef_position) const noexcept {
    return geodesy::Wgs84Ellipsoid::ToEnu(ecef_position, enu_frame_);
}

geodesy::EcefPosition EcefRenderFrame::FromLocal(
    const glm::dvec3& local_meters) const noexcept {
    return geodesy::Wgs84Ellipsoid::FromEnu(local_meters, enu_frame_);
}

}  // namespace earth_map::renderer
