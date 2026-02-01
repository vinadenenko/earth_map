/**
 * @file projection.cpp
 * @brief Map projection system implementation
 */

#include "../../include/earth_map/math/projection.h"
#include <cmath>
#include <stdexcept>
#include <glm/glm.hpp>

namespace earth_map {

// Helper functions
namespace {
    constexpr double PI = 3.14159265358979323846;
    constexpr double WGS84_SEMI_MAJOR_AXIS = 6378137.0;

    inline double DegreesToRadians(double degrees) {
        return glm::radians(degrees);
    }

    inline double RadiansToDegrees(double radians) {
        return glm::degrees(radians);
    }
}

// WebMercatorProjection implementation

Projected WebMercatorProjection::Project(const Geographic& geo) const {
    if (!IsValidLocation(geo)) {
        throw std::invalid_argument("Geographic coordinates outside Web Mercator bounds");
    }

    const double lat_rad = DegreesToRadians(geo.latitude);
    const double lon_rad = DegreesToRadians(geo.longitude);

    const double x = WEB_MERCATOR_HALF_WORLD * lon_rad / PI;
    const double y = WEB_MERCATOR_HALF_WORLD * std::log(std::tan(PI / 4.0 + lat_rad / 2.0)) / PI;

    return Projected(x, y);
}

Geographic WebMercatorProjection::Unproject(const Projected& proj) const {
    const double x = proj.x;
    const double y = proj.y;

    const double lon_rad = x * PI / WEB_MERCATOR_HALF_WORLD;
    const double lat_rad = 2.0 * std::atan(std::exp(y * PI / WEB_MERCATOR_HALF_WORLD)) - PI / 2.0;

    // Clamp latitude to valid range
    const double clamped_lat = std::max(-MAX_LATITUDE, std::min(MAX_LATITUDE,
                                       RadiansToDegrees(lat_rad)));

    return Geographic(clamped_lat, RadiansToDegrees(lon_rad), 0.0);
}

bool WebMercatorProjection::IsValidLocation(const Geographic& geo) const {
    return std::abs(geo.latitude) <= MAX_LATITUDE && geo.IsValid();
}

ProjectedBounds WebMercatorProjection::GetProjectedBounds() const {
    return ProjectedBounds(
        Projected(-WEB_MERCATOR_HALF_WORLD, -WEB_MERCATOR_HALF_WORLD),
        Projected(WEB_MERCATOR_HALF_WORLD, WEB_MERCATOR_HALF_WORLD)
    );
}

double WebMercatorProjection::GetScale(const Geographic& geo) const {
    const double lat_rad = DegreesToRadians(geo.latitude);
    return std::cos(lat_rad) * WGS84_SEMI_MAJOR_AXIS;
}

glm::dvec2 WebMercatorProjection::ToNormalized(const Geographic& geo) const {
    const Projected proj = Project(geo);
    return glm::dvec2(
        (proj.x + WEB_MERCATOR_HALF_WORLD) / WEB_MERCATOR_ORIGIN_SHIFT,
        (proj.y + WEB_MERCATOR_HALF_WORLD) / WEB_MERCATOR_ORIGIN_SHIFT
    );
}

Geographic WebMercatorProjection::FromNormalized(const glm::dvec2& normalized) const {
    const double x = normalized.x * WEB_MERCATOR_ORIGIN_SHIFT - WEB_MERCATOR_HALF_WORLD;
    const double y = normalized.y * WEB_MERCATOR_ORIGIN_SHIFT - WEB_MERCATOR_HALF_WORLD;
    return Unproject(Projected(x, y));
}

// WGS84Projection implementation

Projected WGS84Projection::Project(const Geographic& geo) const {
    if (!IsValidLocation(geo)) {
        throw std::invalid_argument("Invalid geographic coordinates");
    }

    // Identity projection - just convert to projected
    return Projected(geo.longitude, geo.latitude);
}

Geographic WGS84Projection::Unproject(const Projected& proj) const {
    return Geographic(proj.y, proj.x, 0.0);  // y=lat, x=lon
}

bool WGS84Projection::IsValidLocation(const Geographic& geo) const {
    return geo.IsValid();
}

ProjectedBounds WGS84Projection::GetProjectedBounds() const {
    return ProjectedBounds(
        Projected(-180.0, -90.0),
        Projected(180.0, 90.0)
    );
}

double WGS84Projection::GetScale(const Geographic& geo) const {
    // Approximate meters per degree at given latitude
    const double lat_rad = DegreesToRadians(geo.latitude);
    const double meters_per_degree_lat = 111132.954;  // Constant
    const double meters_per_degree_lon = 111132.954 * std::cos(lat_rad);
    return std::sqrt(meters_per_degree_lat * meters_per_degree_lon);
}

// EquirectangularProjection implementation

Projected EquirectangularProjection::Project(const Geographic& geo) const {
    if (!IsValidLocation(geo)) {
        throw std::invalid_argument("Invalid geographic coordinates");
    }

    const double lat_rad = DegreesToRadians(geo.latitude);
    const double lon_rad = DegreesToRadians(geo.longitude);

    // Simple linear projection
    const double x = WGS84_SEMI_MAJOR_AXIS * lon_rad;
    const double y = WGS84_SEMI_MAJOR_AXIS * lat_rad;

    return Projected(x, y);
}

Geographic EquirectangularProjection::Unproject(const Projected& proj) const {
    const double lat = proj.y / WGS84_SEMI_MAJOR_AXIS;
    const double lon = proj.x / WGS84_SEMI_MAJOR_AXIS;

    return Geographic(
        RadiansToDegrees(lat),
        RadiansToDegrees(lon),
        0.0
    );
}

bool EquirectangularProjection::IsValidLocation(const Geographic& geo) const {
    return geo.IsValid();
}

ProjectedBounds EquirectangularProjection::GetProjectedBounds() const {
    const double half_world = PI * WGS84_SEMI_MAJOR_AXIS;
    return ProjectedBounds(
        Projected(-half_world, -half_world),
        Projected(half_world, half_world)
    );
}

double EquirectangularProjection::GetScale(const Geographic& /*geo*/) const {
    return WGS84_SEMI_MAJOR_AXIS;
}

// ProjectionRegistry implementation

std::unordered_map<ProjectionType, std::shared_ptr<Projection>> ProjectionRegistry::projections_;
bool ProjectionRegistry::initialized_ = false;

void ProjectionRegistry::Initialize() {
    if (initialized_) return;
    
    projections_[ProjectionType::WEB_MERCATOR] = std::make_shared<WebMercatorProjection>();
    projections_[ProjectionType::WGS84] = std::make_shared<WGS84Projection>();
    projections_[ProjectionType::EQUIRECTANGULAR] = std::make_shared<EquirectangularProjection>();
    
    initialized_ = true;
}

std::shared_ptr<Projection> ProjectionRegistry::GetProjection(ProjectionType type) {
    Initialize();
    auto it = projections_.find(type);
    if (it != projections_.end()) {
        return it->second;
    }
    throw std::invalid_argument("Projection type not supported");
}

std::shared_ptr<Projection> ProjectionRegistry::GetProjection(int epsg_code) {
    Initialize();
    for (const auto& pair : projections_) {
        if (pair.second->GetEPSGCode() == epsg_code) {
            return pair.second;
        }
    }
    throw std::invalid_argument("EPSG code not supported");
}

std::shared_ptr<Projection> ProjectionRegistry::GetProjection(const std::string& name) {
    Initialize();
    for (const auto& pair : projections_) {
        if (pair.second->GetName() == name) {
            return pair.second;
        }
    }
    throw std::invalid_argument("Projection name not supported");
}

void ProjectionRegistry::RegisterProjection(std::shared_ptr<Projection> projection) {
    if (!projection) {
        throw std::invalid_argument("Cannot register null projection");
    }
    projections_[projection->GetType()] = projection;
}

std::vector<ProjectionType> ProjectionRegistry::GetAvailableProjections() {
    Initialize();
    std::vector<ProjectionType> types;
    for (const auto& pair : projections_) {
        types.push_back(pair.first);
    }
    return types;
}

// ProjectionTransformer implementation

Projected ProjectionTransformer::Transform(const Projected& source_coords,
                                          const Projection& source_proj,
                                          const Projection& target_proj) {
    // Transform via geographic coordinates
    const Geographic geo = source_proj.Unproject(source_coords);
    return target_proj.Project(geo);
}

Projected ProjectionTransformer::TransformGeographic(const Geographic& geo,
                                                     const Projection& /*source_proj*/,
                                                     const Projection& target_proj) {
    return target_proj.Project(geo);
}

glm::dmat3 ProjectionTransformer::CreateTransformationMatrix(const Projection& /*source_proj*/,
                                                            const Projection& /*target_proj*/,
                                                            const Geographic& /*reference_geo*/) {
    // For now, return identity matrix
    // In a full implementation, this would calculate scale, rotation, and translation
    return glm::dmat3(1.0);
}

} // namespace earth_map