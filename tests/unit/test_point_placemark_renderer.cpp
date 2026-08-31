#include <gtest/gtest.h>

#include <earth_map/core/camera_controller.h>
#include <earth_map/earth_map.h>
#include <earth_map/placemarks/placemark_icon_registry.h>
#include <earth_map/placemarks/placemark_layer.h>

#include "../../src/renderer/placemarks/point_placemark_renderer.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

using namespace earth_map;
using namespace earth_map::placemarks;
using namespace earth_map::renderer::placemarks;

namespace {

std::vector<std::uint8_t> MakeSolidIconPixels(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                              std::uint8_t a) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(PlacemarkIconRegistry::kIconCellSize) *
        PlacemarkIconRegistry::kIconCellSize * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
    return pixels;
}

PlacemarkFeature MakePoint(PlacemarkId id, double longitude_degrees, double latitude_degrees,
                           std::string icon_key) {
    PointPlacemark point;
    point.metadata.id = id;
    point.position.latitude_radians = glm::radians(latitude_degrees);
    point.position.longitude_radians = glm::radians(longitude_degrees);
    point.position.ellipsoid_height_meters = 0.0;
    point.icon.icon_key = std::move(icon_key);
    point.icon.scale = 2.0f;
    point.icon.color = {0.1f, 0.2f, 0.3f, 0.4f};
    return point;
}

class PointPlacemarkRendererTest : public ::testing::Test {
protected:
    void SetUp() override {
        placemark_layer_ = PlacemarkLayer::Create();
        icon_registry_ = PlacemarkIconRegistry::Create();
        ASSERT_TRUE(icon_registry_->Register(
            "pin", PlacemarkIconRegistry::kIconCellSize, PlacemarkIconRegistry::kIconCellSize,
            MakeSolidIconPixels(255, 0, 0, 255)));

        Configuration config;
        config.screen_width = 1920;
        config.screen_height = 1080;
        camera_ = CreateCameraController(config);
        ASSERT_NE(camera_, nullptr);
        ASSERT_TRUE(camera_->Initialize());

        // Camera above (0, 0) looking straight down at the surface -- puts
        // the sub-camera point (and anything near it) inside the frustum,
        // and the antipodal point on the far side of the globe, which
        // horizon-occlusion culling must reject regardless of frustum.
        camera_->SetGeographicPosition(0.0, 0.0, 500'000.0);
    }

    void TearDown() override { delete camera_; }

    std::shared_ptr<PlacemarkLayer> placemark_layer_;
    std::shared_ptr<PlacemarkIconRegistry> icon_registry_;
    CameraController* camera_ = nullptr;
};

}  // namespace

TEST_F(PointPlacemarkRendererTest, VisiblePointNearSubCameraPointProducesInstance) {
    PlacemarkChangeSet changes;
    changes.upserts.push_back(MakePoint(1, 0.5, 0.5, "pin"));
    ASSERT_TRUE(placemark_layer_->Apply(changes).applied);

    PointPlacemarkRenderer renderer(/*skip_gl_init=*/true);
    const auto instances = renderer.BuildInstances(
        placemark_layer_->Snapshot(), icon_registry_->Snapshot(),
        *geodesy::Wgs84Ellipsoid::FromEcef(camera_->GetEcefPosition()),
        camera_->GetViewMatrix(), camera_->GetProjectionMatrix(16.0f / 9.0f));

    ASSERT_EQ(instances.size(), 1U);
    EXPECT_NEAR(instances[0].scale, 2.0f, 1e-6f);
    EXPECT_NEAR(instances[0].color.r, 0.1f, 1e-6f);
    EXPECT_NEAR(instances[0].color.a, 0.4f, 1e-6f);
    // Atlas UV rect must be a real (non-degenerate) sub-rectangle of [0,1]^2.
    EXPECT_GE(instances[0].atlas_uv_min.x, 0.0f);
    EXPECT_GE(instances[0].atlas_uv_min.y, 0.0f);
    EXPECT_LE(instances[0].atlas_uv_max.x, 1.0f);
    EXPECT_LE(instances[0].atlas_uv_max.y, 1.0f);
    EXPECT_GT(instances[0].atlas_uv_max.x, instances[0].atlas_uv_min.x);
    EXPECT_GT(instances[0].atlas_uv_max.y, instances[0].atlas_uv_min.y);
}

TEST_F(PointPlacemarkRendererTest, AntipodalPointIsHorizonCulled) {
    PlacemarkChangeSet changes;
    changes.upserts.push_back(MakePoint(1, 179.9, 0.0, "pin"));
    ASSERT_TRUE(placemark_layer_->Apply(changes).applied);

    PointPlacemarkRenderer renderer(/*skip_gl_init=*/true);
    const auto instances = renderer.BuildInstances(
        placemark_layer_->Snapshot(), icon_registry_->Snapshot(),
        *geodesy::Wgs84Ellipsoid::FromEcef(camera_->GetEcefPosition()),
        camera_->GetViewMatrix(), camera_->GetProjectionMatrix(16.0f / 9.0f));

    EXPECT_TRUE(instances.empty());
}

TEST_F(PointPlacemarkRendererTest, UnregisteredIconKeyIsSkipped) {
    PlacemarkChangeSet changes;
    changes.upserts.push_back(MakePoint(1, 0.5, 0.5, "no-such-icon"));
    ASSERT_TRUE(placemark_layer_->Apply(changes).applied);

    PointPlacemarkRenderer renderer(/*skip_gl_init=*/true);
    const auto instances = renderer.BuildInstances(
        placemark_layer_->Snapshot(), icon_registry_->Snapshot(),
        *geodesy::Wgs84Ellipsoid::FromEcef(camera_->GetEcefPosition()),
        camera_->GetViewMatrix(), camera_->GetProjectionMatrix(16.0f / 9.0f));

    EXPECT_TRUE(instances.empty());
}

TEST_F(PointPlacemarkRendererTest, NonPointFeatureIsIgnored) {
    PlacemarkChangeSet changes;
    LineStringPlacemark line;
    line.metadata.id = 1;
    line.positions.push_back({glm::radians(0.4), glm::radians(0.4), 0.0});
    line.positions.push_back({glm::radians(0.6), glm::radians(0.6), 0.0});
    changes.upserts.push_back(line);
    ASSERT_TRUE(placemark_layer_->Apply(changes).applied);

    PointPlacemarkRenderer renderer(/*skip_gl_init=*/true);
    const auto instances = renderer.BuildInstances(
        placemark_layer_->Snapshot(), icon_registry_->Snapshot(),
        *geodesy::Wgs84Ellipsoid::FromEcef(camera_->GetEcefPosition()),
        camera_->GetViewMatrix(), camera_->GetProjectionMatrix(16.0f / 9.0f));

    EXPECT_TRUE(instances.empty());
}

TEST_F(PointPlacemarkRendererTest, InvisibleMetadataFlagExcludesPoint) {
    PointPlacemark point =
        std::get<PointPlacemark>(MakePoint(1, 0.5, 0.5, "pin"));
    point.metadata.visible = false;

    PlacemarkChangeSet changes;
    changes.upserts.push_back(point);
    ASSERT_TRUE(placemark_layer_->Apply(changes).applied);

    PointPlacemarkRenderer renderer(/*skip_gl_init=*/true);
    const auto instances = renderer.BuildInstances(
        placemark_layer_->Snapshot(), icon_registry_->Snapshot(),
        *geodesy::Wgs84Ellipsoid::FromEcef(camera_->GetEcefPosition()),
        camera_->GetViewMatrix(), camera_->GetProjectionMatrix(16.0f / 9.0f));

    EXPECT_TRUE(instances.empty());
}

TEST_F(PointPlacemarkRendererTest, DistinctIconsGetDistinctAtlasRects) {
    ASSERT_TRUE(icon_registry_->Register(
        "flag", PlacemarkIconRegistry::kIconCellSize, PlacemarkIconRegistry::kIconCellSize,
        MakeSolidIconPixels(0, 255, 0, 255)));

    PlacemarkChangeSet changes;
    changes.upserts.push_back(MakePoint(1, 0.2, 0.2, "pin"));
    changes.upserts.push_back(MakePoint(2, 0.3, 0.3, "flag"));
    ASSERT_TRUE(placemark_layer_->Apply(changes).applied);

    PointPlacemarkRenderer renderer(/*skip_gl_init=*/true);
    const auto instances = renderer.BuildInstances(
        placemark_layer_->Snapshot(), icon_registry_->Snapshot(),
        *geodesy::Wgs84Ellipsoid::FromEcef(camera_->GetEcefPosition()),
        camera_->GetViewMatrix(), camera_->GetProjectionMatrix(16.0f / 9.0f));

    ASSERT_EQ(instances.size(), 2U);
    EXPECT_NE(instances[0].atlas_uv_min, instances[1].atlas_uv_min);
}
