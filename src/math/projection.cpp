/**
 * @file projection.cpp
 * @brief Map projection system implementation
 */

#include "../../include/earth_map/math/projection.h"
#include <cmath>
#include <stdexcept>

namespace earth_map {

// WebMercatorProjection implementation

ProjectedCoordinates WebMercatorProjection::Project(const GeographicCoordinates& geo) const {
    if (!IsValidLocation(geo)) {
        throw std::invalid_argument("Geographic coordinates outside Web Mercator bounds");
    }
    
    const double lat_rad = geo.LatitudeRadians();
    const double lon_rad = geo.LongitudeRadians();
    
    const double x = WEB_MERCATOR_HALF_WORLD * lon_rad / M_PI;
    const double y = WEB_MERCATOR_HALF_WORLD * std::log(std::tan(M_PI_4 + lat_rad / 2.0)) / M_PI;
    
    return ProjectedCoordinates(x, y);
}

GeographicCoordinates WebMercatorProjection::Unproject(const ProjectedCoordinates& proj) const {
    const double x = proj.x;
    const double y = proj.y;
    
    const double lon_rad = x * M_PI / WEB_MERCATOR_HALF_WORLD;
    const double lat_rad = 2.0 * std::atan(std::exp(y * M_PI / WEB_MERCATOR_HALF_WORLD)) - M_PI_2;
    
    // Clamp latitude to valid range
    const double clamped_lat = std::max(-MAX_LATITUDE, std::min(MAX_LATITUDE, 
                                       CoordinateSystem::RadiansToDegrees(lat_rad)));
    
    return GeographicCoordinates(clamped_lat, CoordinateSystem::RadiansToDegrees(lon_rad), 0.0);
}

bool WebMercatorProjection::IsValidLocation(const GeographicCoordinates& geo) const {
    return std::abs(geo.latitude) <= MAX_LATITUDE && geo.IsValid();
}

BoundingBox2D WebMercatorProjection::GetProjectedBounds() const {
    return BoundingBox2D(
        glm::dvec2(-WEB_MERCATOR_HALF_WORLD, -WEB_MERCATOR_HALF_WORLD),
        glm::dvec2(WEB_MERCATOR_HALF_WORLD, WEB_MERCATOR_HALF_WORLD)
    );
}

double WebMercatorProjection::GetScale(const GeographicCoordinates& geo) const {
    const double lat_rad = geo.LatitudeRadians();
    return std::cos(lat_rad) * WGS84Ellipsoid::SEMI_MAJOR_AXIS;
}

glm::dvec2 WebMercatorProjection::ToNormalized(const GeographicCoordinates& geo) const {
    const ProjectedCoordinates proj = Project(geo);
    return glm::dvec2(
        (proj.x + WEB_MERCATOR_HALF_WORLD) / WEB_MERCATOR_ORIGIN_SHIFT,
        (proj.y + WEB_MERCATOR_HALF_WORLD) / WEB_MERCATOR_ORIGIN_SHIFT
    );
}

GeographicCoordinates WebMercatorProjection::FromNormalized(const glm::dvec2& normalized) const {
    const double x = normalized.x * WEB_MERCATOR_ORIGIN_SHIFT - WEB_MERCATOR_HALF_WORLD;
    const double y = normalized.y * WEB_MERCATOR_ORIGIN_SHIFT - WEB_MERCATOR_HALF_WORLD;
    return Unproject(ProjectedCoordinates(x, y));
}

// WGS84Projection implementation

ProjectedCoordinates WGS84Projection::Project(const GeographicCoordinates& geo) const {
    if (!IsValidLocation(geo)) {
        throw std::invalid_argument("Invalid geographic coordinates");
    }
    
    // Identity projection - just convert to double
    return ProjectedCoordinates(geo.longitude, geo.latitude);
}

GeographicCoordinates WGS84Projection::Unproject(const ProjectedCoordinates& proj) const {
    return GeographicCoordinates(proj.y, proj.x, 0.0);  // y=lat, x=lon
}

bool WGS84Projection::IsValidLocation(const GeographicCoordinates& geo) const {
    return geo.IsValid();
}

BoundingBox2D WGS84Projection::GetProjectedBounds() const {
    return BoundingBox2D(
        glm::dvec2(-180.0, -90.0),
        glm::dvec2(180.0, 90.0)
    );
}

double WGS84Projection::GetScale(const GeographicCoordinates& geo) const {
    // Approximate meters per degree at given latitude
    const double lat_rad = geo.LatitudeRadians();
    const double meters_per_degree_lat = 111132.954;  // Constant
    const double meters_per_degree_lon = 111132.954 * std::cos(lat_rad);
    return std::sqrt(meters_per_degree_lat * meters_per_degree_lon);
}

// EquirectangularProjection implementation

ProjectedCoordinates EquirectangularProjection::Project(const GeographicCoordinates& geo) const {
    if (!IsValidLocation(geo)) {
        throw std::invalid_argument("Invalid geographic coordinates");
    }
    
    const double lat_rad = geo.LatitudeRadians();
    const double lon_rad = geo.LongitudeRadians();
    
    // Simple linear projection
    const double x = WGS84Ellipsoid::SEMI_MAJOR_AXIS * lon_rad;
    const double y = WGS84Ellipsoid::SEMI_MAJOR_AXIS * lat_rad;
    
    return ProjectedCoordinates(x, y);
}

GeographicCoordinates EquirectangularProjection::Unproject(const ProjectedCoordinates& proj) const {
    const double lat = proj.y / WGS84Ellipsoid::SEMI_MAJOR_AXIS;
    const double lon = proj.x / WGS84Ellipsoid::SEMI_MAJOR_AXIS;
    
    return GeographicCoordinates(
        CoordinateSystem::RadiansToDegrees(lat),
        CoordinateSystem::RadiansToDegrees(lon),
        0.0
    );
}

bool EquirectangularProjection::IsValidLocation(const GeographicCoordinates& geo) const {
    return geo.IsValid();
}

BoundingBox2D EquirectangularProjection::GetProjectedBounds() const {
    const double half_world = M_PI * WGS84Ellipsoid::SEMI_MAJOR_AXIS;
    return BoundingBox2D(
        glm::dvec2(-half_world, -half_world),
        glm::dvec2(half_world, half_world)
    );
}

double EquirectangularProjection::GetScale(const GeographicCoordinates& /*geo*/) const {
    return WGS84Ellipsoid::SEMI_MAJOR_AXIS;
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

ProjectedCoordinates ProjectionTransformer::Transform(const ProjectedCoordinates& source_coords,
                                                     const Projection& source_proj,
                                                     const Projection& target_proj) {
    // Transform via geographic coordinates
    const GeographicCoordinates geo = source_proj.Unproject(source_coords);
    return target_proj.Project(geo);
}

ProjectedCoordinates ProjectionTransformer::TransformGeographic(const GeographicCoordinates& geo,
                                                                const Projection& /*source_proj*/,
                                                                const Projection& target_proj) {
    return target_proj.Project(geo);
}

glm::dmat3 ProjectionTransformer::CreateTransformationMatrix(const Projection& /*source_proj*/,
                                                           const Projection& /*target_proj*/,
                                                           const GeographicCoordinates& /*reference_geo*/) {
    // For now, return identity matrix
    // In a full implementation, this would calculate scale, rotation, and translation
    return glm::dmat3(1.0);
}

} // namespace earth_map