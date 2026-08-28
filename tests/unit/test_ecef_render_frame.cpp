#include <gtest/gtest.h>

#include "../../src/renderer/ecef_render_frame.h"

namespace earth_map::renderer::tests {

TEST(EcefRenderFrameTest, CameraOriginMapsToLocalZero) {
    const geodesy::GeodeticPosition camera{0.7, -1.2, 1500.0};
    const EcefRenderFrame frame = EcefRenderFrame::FromCamera(camera);

    const glm::dvec3 local = frame.ToLocal(frame.CameraOrigin());
    EXPECT_NEAR(local.x, 0.0, 1e-9);
    EXPECT_NEAR(local.y, 0.0, 1e-9);
    EXPECT_NEAR(local.z, 0.0, 1e-9);
}

TEST(EcefRenderFrameTest, RoundTripsGlobalEcefMetres) {
    const geodesy::GeodeticPosition camera{0.7, -1.2, 1500.0};
    const geodesy::GeodeticPosition target{0.7002, -1.1995, 250.0};
    const EcefRenderFrame frame = EcefRenderFrame::FromCamera(camera);
    const geodesy::EcefPosition target_ecef = geodesy::Wgs84Ellipsoid::ToEcef(target);

    const geodesy::EcefPosition round_tripped = frame.FromLocal(frame.ToLocal(target_ecef));
    EXPECT_NEAR(round_tripped.meters.x, target_ecef.meters.x, 1e-6);
    EXPECT_NEAR(round_tripped.meters.y, target_ecef.meters.y, 1e-6);
    EXPECT_NEAR(round_tripped.meters.z, target_ecef.meters.z, 1e-6);
}

}  // namespace earth_map::renderer::tests
