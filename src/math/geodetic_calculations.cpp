/**
 * @file geodetic_calculations.cpp
 * @brief Geodetic calculations implementation
 */

#include "../../include/earth_map/math/geodetic_calculations.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <glm/glm.hpp>

namespace earth_map {

// Helper functions and constants
namespace {
    // WGS84 ellipsoid constants
    constexpr double WGS84_SEMI_MAJOR_AXIS = 6378137.0;  // meters
    constexpr double WGS84_SEMI_MINOR_AXIS = 6356752.314245;  // meters
    constexpr double WGS84_FLATTENING = 1.0 / 298.257223563;

    inline double DegreesToRadians(double degrees) {
        return glm::radians(degrees);
    }

    inline double RadiansToDegrees(double radians) {
        return glm::degrees(radians);
    }

    // Normalize angle to [0, 2π)
    inline double NormalizeAngleRadians(double angle_rad) {
        constexpr double TWO_PI = 2.0 * M_PI;
        double normalized = std::fmod(angle_rad, TWO_PI);
        if (normalized < 0.0) {
            normalized += TWO_PI;
        }
        return normalized;
    }
}

// GeodeticCalculator implementation

double GeodeticCalculator::GeodeticCalculator::HaversineDistance(const Geographic& point1,
                                             const Geographic& point2) {
    const DistanceResult result = GeodeticCalculator::HaversineDistanceAndBearing(point1, point2);
    return result.distance;
}

DistanceResult GeodeticCalculator::GeodeticCalculator::HaversineDistanceAndBearing(const Geographic& point1,
                                                              const Geographic& point2) {
    const double lat1_rad = DegreesToRadians(point1.latitude);
    const double lat2_rad = DegreesToRadians(point2.latitude);
    const double delta_lat = lat2_rad - lat1_rad;
    const double delta_lon = DegreesToRadians(point2.longitude) - DegreesToRadians(point1.longitude);
    
    const double sin_delta_lat_2 = std::sin(delta_lat / 2.0);
    const double sin_delta_lon_2 = std::sin(delta_lon / 2.0);
    
    const double a = sin_delta_lat_2 * sin_delta_lat_2 +
                     std::cos(lat1_rad) * std::cos(lat2_rad) *
                     sin_delta_lon_2 * sin_delta_lon_2;
    
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    const double distance = WGS84_SEMI_MAJOR_AXIS * c;
    
    // Calculate initial bearing
    const double y = std::sin(delta_lon) * std::cos(lat2_rad);
    const double x = std::cos(lat1_rad) * std::sin(lat2_rad) -
                     std::sin(lat1_rad) * std::cos(lat2_rad) * std::cos(delta_lon);
    
    double initial_bearing = std::atan2(y, x);
    initial_bearing = NormalizeAngleRadians(initial_bearing);
    const double initial_bearing_deg = RadiansToDegrees(initial_bearing);
    
    // Calculate final bearing
    const double y_final = std::sin(delta_lon) * std::cos(lat1_rad);
    const double x_final = std::cos(lat2_rad) * std::sin(lat1_rad) -
                          std::sin(lat2_rad) * std::cos(lat1_rad) * std::cos(delta_lon);
    
    double final_bearing = std::atan2(y_final, x_final) + M_PI;
    final_bearing = NormalizeAngleRadians(final_bearing);
    const double final_bearing_deg = RadiansToDegrees(final_bearing);
    
    return DistanceResult(distance, initial_bearing_deg, final_bearing_deg);
}

double GeodeticCalculator::VincentyDistance(const Geographic& point1,
                                           const Geographic& point2) {
    const DistanceResult result = VincentyDistanceAndBearing(point1, point2);
    return result.distance;
}

DistanceResult GeodeticCalculator::VincentyDistanceAndBearing(const Geographic& point1,
                                                             const Geographic& point2) {
    const double a = WGS84_SEMI_MAJOR_AXIS;
    const double b = WGS84_SEMI_MINOR_AXIS;
    const double f = WGS84_FLATTENING;
    
    const double lat1_rad = DegreesToRadians(point1.latitude);
    const double lat2_rad = DegreesToRadians(point2.latitude);
    const double lon1_rad = DegreesToRadians(point1.longitude);
    const double lon2_rad = DegreesToRadians(point2.longitude);
    
    const double U1 = std::atan((1.0 - f) * std::tan(lat1_rad));
    const double U2 = std::atan((1.0 - f) * std::tan(lat2_rad));
    const double sin_U1 = std::sin(U1);
    const double cos_U1 = std::cos(U1);
    const double sin_U2 = std::sin(U2);
    const double cos_U2 = std::cos(U2);
    
    double L = lon2_rad - lon1_rad;
    double lambda = L;
    double lambda_prev;
    double sin_sigma, cos_sigma, sigma, sin_alpha, cos2_alpha, cos2_sigma_m;
    
    // Iterative solution
    const int max_iterations = 100;
    const double tolerance = 1e-12;
    
    for (int i = 0; i < max_iterations; ++i) {
        const double sin_lambda = std::sin(lambda);
        const double cos_lambda = std::cos(lambda);
        
        sin_sigma = std::sqrt((cos_U2 * sin_lambda) * (cos_U2 * sin_lambda) +
                              (cos_U1 * sin_U2 - sin_U1 * cos_U2 * cos_lambda) *
                              (cos_U1 * sin_U2 - sin_U1 * cos_U2 * cos_lambda));
        
        if (sin_sigma == 0.0) {
            return DistanceResult(0.0, 0.0, 0.0);  // Co-incident points
        }
        
        cos_sigma = sin_U1 * sin_U2 + cos_U1 * cos_U2 * cos_lambda;
        sigma = std::atan2(sin_sigma, cos_sigma);
        
        sin_alpha = cos_U1 * cos_U2 * sin_lambda / sin_sigma;
        cos2_alpha = 1.0 - sin_alpha * sin_alpha;
        
        if (cos2_alpha == 0.0) {
            cos2_sigma_m = 0.0;  // Equatorial line
        } else {
            cos2_sigma_m = cos_sigma - 2.0 * sin_U1 * sin_U2 / cos2_alpha;
        }
        
        const double C = f / 16.0 * cos2_alpha * (4.0 + f * (4.0 - 3.0 * cos2_alpha));
        lambda_prev = lambda;
        lambda = L + (1.0 - C) * f * sin_alpha *
                  (sigma + C * sin_sigma * (cos2_sigma_m + C * cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m)));
        
        if (std::abs(lambda - lambda_prev) < tolerance) {
            break;  // Converged
        }
        
        if (i == max_iterations - 1) {
            throw std::runtime_error("Vincenty formula failed to converge");
        }
    }
    
    const double u2 = cos2_alpha * (a * a - b * b) / (b * b);
    const double A = 1.0 + u2 / 16384.0 * (4096.0 + u2 * (-768.0 + u2 * (320.0 - 175.0 * u2)));
    const double B = u2 / 1024.0 * (256.0 + u2 * (-128.0 + u2 * (74.0 - 47.0 * u2)));
    
    const double delta_sigma = B * sin_sigma *
                               (cos2_sigma_m + B / 4.0 *
                                (cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m) -
                                 B / 6.0 * cos2_sigma_m * (-3.0 + 4.0 * sin_sigma * sin_sigma) *
                                 (-3.0 + 4.0 * cos2_sigma_m * cos2_sigma_m)));
    
    const double distance = b * A * (sigma - delta_sigma);
    
    // Calculate initial bearing
    const double lambda_rad = lambda;
    double initial_bearing = std::atan2(cos_U2 * std::sin(lambda_rad),
                                       cos_U1 * sin_U2 - sin_U1 * cos_U2 * std::cos(lambda_rad));
    initial_bearing = NormalizeAngleRadians(initial_bearing);
    const double initial_bearing_deg = RadiansToDegrees(initial_bearing);
    
    // Calculate final bearing
    double final_bearing = std::atan2(cos_U1 * std::sin(lambda_rad),
                                     -sin_U1 * cos_U2 + cos_U1 * sin_U2 * std::cos(lambda_rad));
    final_bearing = NormalizeAngleRadians(final_bearing);
    const double final_bearing_deg = RadiansToDegrees(final_bearing);
    
    return DistanceResult(distance, initial_bearing_deg, final_bearing_deg);
}

double GeodeticCalculator::InitialBearing(const Geographic& point1,
                                         const Geographic& point2) {
    const DistanceResult result = GeodeticCalculator::HaversineDistanceAndBearing(point1, point2);
    return result.initial_bearing;
}

double GeodeticCalculator::FinalBearing(const Geographic& point1,
                                       const Geographic& point2) {
    const DistanceResult result = GeodeticCalculator::HaversineDistanceAndBearing(point1, point2);
    return result.final_bearing;
}

Geographic GeodeticCalculator::GeodeticCalculator::DestinationPoint(const Geographic& start,
                                                         double bearing,
                                                         double distance) {
    const double a = WGS84_SEMI_MAJOR_AXIS;
    const double angular_distance = distance / a;
    
    const double lat1_rad = DegreesToRadians(start.latitude);
    const double bearing_rad = DegreesToRadians(bearing);
    
    const double lat2_rad = std::asin(std::sin(lat1_rad) * std::cos(angular_distance) +
                                       std::cos(lat1_rad) * std::sin(angular_distance) * std::cos(bearing_rad));
    
    const double lon2_rad = DegreesToRadians(start.longitude) +
                           std::atan2(std::sin(bearing_rad) * std::sin(angular_distance) * std::cos(lat1_rad),
                                     std::cos(angular_distance) - std::sin(lat1_rad) * std::sin(lat2_rad));
    
    return Geographic(
        RadiansToDegrees(lat2_rad),
        RadiansToDegrees(lon2_rad),
        start.altitude
    );
}

Geographic GeodeticCalculator::IntersectionPoint(const Geographic& point1,
                                                            double bearing1,
                                                            const Geographic& point2,
                                                            double bearing2) {
    const double bearing1_rad = DegreesToRadians(bearing1);
    const double bearing2_rad = DegreesToRadians(bearing2);
    
    (void)bearing1_rad; // Suppress unused variable warning
    (void)bearing2_rad; // Suppress unused variable warning
    
    // This is a simplified implementation - a full implementation would be more complex
    // For now, return midpoint as approximation
    return Geographic(
        (point1.latitude + point2.latitude) / 2.0,
        (point1.longitude + point2.longitude) / 2.0,
        (point1.altitude + point2.altitude) / 2.0
    );
}

double GeodeticCalculator::CrossTrackDistance(const Geographic& point,
                                             const Geographic& path_start,
                                             const Geographic& path_end) {
    const double distance_start_to_point = GeodeticCalculator::HaversineDistance(path_start, point);
    const double initial_bearing = InitialBearing(path_start, path_end);
    const double bearing_to_point = InitialBearing(path_start, point);
    
    const double delta_bearing = std::sin(bearing_to_point - initial_bearing);
    
    return std::abs(distance_start_to_point * delta_bearing);
}

double GeodeticCalculator::AlongTrackDistance(const Geographic& point,
                                              const Geographic& path_start,
                                              const Geographic& path_end) {
    const double distance_start_to_point = GeodeticCalculator::HaversineDistance(path_start, point);
    const double initial_bearing = InitialBearing(path_start, path_end);
    const double bearing_to_point = InitialBearing(path_start, point);
    
    const double cos_bearing_diff = std::cos(bearing_to_point - initial_bearing);
    
    return distance_start_to_point * cos_bearing_diff;
}

// GeographicBounds implementation

BoundingBox2D GeographicBounds::FromCenterRadius(const Geographic& center, double radius) {
    const Geographic north = GeodeticCalculator::GeodeticCalculator::DestinationPoint(center, 0.0, radius);
    const Geographic south = GeodeticCalculator::GeodeticCalculator::DestinationPoint(center, 180.0, radius);
    const Geographic east = GeodeticCalculator::GeodeticCalculator::DestinationPoint(center, 90.0, radius);
    const Geographic west = GeodeticCalculator::GeodeticCalculator::DestinationPoint(center, 270.0, radius);
    
    return BoundingBox2D(
        glm::dvec2(west.longitude, south.latitude),
        glm::dvec2(east.longitude, north.latitude)
    );
}

BoundingBox2D GeographicBounds::FromPoints(const std::vector<Geographic>& points) {
    if (points.empty()) {
        return BoundingBox2D();
    }
    
    double min_lat = points[0].latitude;
    double max_lat = points[0].latitude;
    double min_lon = points[0].longitude;
    double max_lon = points[0].longitude;
    
    for (const auto& point : points) {
        min_lat = std::min(min_lat, point.latitude);
        max_lat = std::max(max_lat, point.latitude);
        min_lon = std::min(min_lon, point.longitude);
        max_lon = std::max(max_lon, point.longitude);
    }
    
    return BoundingBox2D(
        glm::dvec2(min_lon, min_lat),
        glm::dvec2(max_lon, max_lat)
    );
}

BoundingBox2D GeographicBounds::Expand(const BoundingBox2D& bounds, double distance) {
    const Geographic center(bounds.GetCenter().y, bounds.GetCenter().x, 0.0);
    const Geographic north = GeodeticCalculator::DestinationPoint(center, 0.0, distance);
    const Geographic south = GeodeticCalculator::DestinationPoint(center, 180.0, distance);
    const Geographic east = GeodeticCalculator::DestinationPoint(center, 90.0, distance);
    const Geographic west = GeodeticCalculator::DestinationPoint(center, 270.0, distance);
    
    return BoundingBox2D(
        glm::dvec2(std::min(static_cast<double>(bounds.min.x), west.longitude), std::min(static_cast<double>(bounds.min.y), south.latitude)),
        glm::dvec2(std::max(static_cast<double>(bounds.max.x), east.longitude), std::max(static_cast<double>(bounds.max.y), north.latitude))
    );
}

double GeographicBounds::CalculateArea(const BoundingBox2D& bounds) {
    // Simplified area calculation using rectangular approximation
    const Geographic sw(bounds.min.y, bounds.min.x, 0.0);
    const Geographic ne(bounds.max.y, bounds.max.x, 0.0);
    
    const double width_meters = GeodeticCalculator::HaversineDistance(
        Geographic(sw.latitude, sw.longitude, 0.0),
        Geographic(sw.latitude, ne.longitude, 0.0)
    );
    
    const double height_meters = GeodeticCalculator::HaversineDistance(
        Geographic(sw.latitude, sw.longitude, 0.0),
        Geographic(ne.latitude, sw.longitude, 0.0)
    );
    
    return width_meters * height_meters;
}

bool GeographicBounds::Intersect(const BoundingBox2D& bounds1, const BoundingBox2D& bounds2) {
    return bounds1.Intersects(bounds2);
}

BoundingBox2D GeographicBounds::Merge(const BoundingBox2D& bounds1, const BoundingBox2D& bounds2) {
    BoundingBox2D result;
    result.min.x = std::min(bounds1.min.x, bounds2.min.x);
    result.min.y = std::min(bounds1.min.y, bounds2.min.y);
    result.max.x = std::max(bounds1.max.x, bounds2.max.x);
    result.max.y = std::max(bounds1.max.y, bounds2.max.y);
    return result;
}

BoundingBox2D GeographicBounds::ToProjected(const BoundingBox2D& geo_bounds,
                                            const Projection& projection) {
    const Geographic sw(geo_bounds.min.y, geo_bounds.min.x, 0.0);
    const Geographic ne(geo_bounds.max.y, geo_bounds.max.x, 0.0);
    
    const Projected sw_proj = projection.Project(sw);
    const Projected ne_proj = projection.Project(ne);
    
    return BoundingBox2D(
        glm::dvec2(sw_proj.x, sw_proj.y),
        glm::dvec2(ne_proj.x, ne_proj.y)
    );
}

BoundingBox2D GeographicBounds::FromProjected(const BoundingBox2D& proj_bounds,
                                             const Projection& projection) {
    const Projected sw(proj_bounds.min.x, proj_bounds.min.y);
    const Projected ne(proj_bounds.max.x, proj_bounds.max.y);
    
    const Geographic sw_geo = projection.Unproject(sw);
    const Geographic ne_geo = projection.Unproject(ne);
    
    return BoundingBox2D(
        glm::dvec2(sw_geo.longitude, sw_geo.latitude),
        glm::dvec2(ne_geo.longitude, ne_geo.latitude)
    );
}

// GeodeticPath implementation (simplified versions for now)

std::vector<Geographic> GeodeticPath::Simplify(const std::vector<Geographic>& points,
                                                           double /*tolerance*/) {
    // Simplified implementation - just return original points
    // Full Douglas-Peucker algorithm would be implemented here
    return points;
}

double GeodeticPath::CalculateLength(const std::vector<Geographic>& points) {
    if (points.size() < 2) return 0.0;
    
    double total_length = 0.0;
    for (size_t i = 1; i < points.size(); ++i) {
        total_length += GeodeticCalculator::HaversineDistance(points[i-1], points[i]);
    }
    return total_length;
}

Geographic GeodeticPath::Interpolate(const std::vector<Geographic>& points,
                                                double distance) {
    if (points.empty()) return Geographic();
    if (points.size() == 1) return points[0];
    
    double accumulated_distance = 0.0;
    for (size_t i = 1; i < points.size(); ++i) {
        const double segment_length = GeodeticCalculator::HaversineDistance(points[i-1], points[i]);
        if (accumulated_distance + segment_length >= distance) {
            const double remaining_distance = distance - accumulated_distance;
            const double ratio = remaining_distance / segment_length;
            
            // Linear interpolation (simplified - should use geodetic interpolation)
            return Geographic(
                points[i-1].latitude + ratio * (points[i].latitude - points[i-1].latitude),
                points[i-1].longitude + ratio * (points[i].longitude - points[i-1].longitude),
                points[i-1].altitude + ratio * (points[i].altitude - points[i-1].altitude)
            );
        }
        accumulated_distance += segment_length;
    }
    
    return points.back();
}

std::vector<Geographic> GeodeticPath::Sample(const std::vector<Geographic>& points,
                                                          double interval) {
    std::vector<Geographic> sampled_points;
    const double total_length = CalculateLength(points);
    
    for (double distance = 0.0; distance <= total_length; distance += interval) {
        sampled_points.push_back(Interpolate(points, distance));
    }
    
    return sampled_points;
}

double GeodeticPath::CalculateArea(const std::vector<Geographic>& points) {
    if (points.size() < 3) return 0.0;
    
    // Simplified area calculation using shoelace formula
    // Full implementation would use spherical excess formula
    double area = 0.0;
    const size_t n = points.size();
    
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        area += points[i].longitude * points[j].latitude;
        area -= points[j].longitude * points[i].latitude;
    }
    
    return std::abs(area) * 111320.0 * 111320.0;  // Convert to square meters (approximate)
}

Geographic GeodeticPath::CalculateCentroid(const std::vector<Geographic>& points) {
    if (points.empty()) return Geographic();
    
    double sum_lat = 0.0;
    double sum_lon = 0.0;
    double sum_alt = 0.0;
    
    for (const auto& point : points) {
        sum_lat += point.latitude;
        sum_lon += point.longitude;
        sum_alt += point.altitude;
    }
    
    return Geographic(
        sum_lat / points.size(),
        sum_lon / points.size(),
        sum_alt / points.size()
    );
}

bool GeodeticPath::PointInPolygon(const Geographic& point,
                                  const std::vector<Geographic>& polygon) {
    if (polygon.size() < 3) return false;
    
    // Ray casting algorithm (simplified 2D version)
    bool inside = false;
    const size_t n = polygon.size();
    
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((polygon[i].latitude > point.latitude) != (polygon[j].latitude > point.latitude)) &&
            (point.longitude < (polygon[j].longitude - polygon[i].longitude) *
             (point.latitude - polygon[i].latitude) / (polygon[j].latitude - polygon[i].latitude) + polygon[i].longitude)) {
            inside = !inside;
        }
    }
    
    return inside;
}

// TerrainCalculator implementation (simplified versions)

double TerrainCalculator::CalculateSlope(const Geographic& point1,
                                         const Geographic& point2) {
    const double horizontal_distance = GeodeticCalculator::HaversineDistance(point1, point2);
    const double vertical_distance = point2.altitude - point1.altitude;
    
    if (horizontal_distance == 0.0) return 0.0;
    
    return std::abs(std::atan(vertical_distance / horizontal_distance) * 180.0 / M_PI);
}

double TerrainCalculator::CalculateAspect(const Geographic& /*center*/,
                                          const std::array<Geographic, 8>& neighbors) {
    // Simplified aspect calculation
    // Full implementation would use Horn's method
    const double dx = GeodeticCalculator::HaversineDistance(neighbors[1], neighbors[3]);
    const double dy = GeodeticCalculator::HaversineDistance(neighbors[0], neighbors[2]);
    
    double aspect = std::atan2(dy, dx) * 180.0 / M_PI;
    aspect = 90.0 - aspect;  // Convert to compass bearing
    if (aspect < 0.0) aspect += 360.0;
    
    return aspect;
}

bool TerrainCalculator::LineOfSight(const Geographic& observer,
                                    const Geographic& target,
                                    const std::vector<double>& /*terrain_heights*/) {
    // Simplified line of sight check
    // Full implementation would check intermediate terrain heights
    const double distance = GeodeticCalculator::HaversineDistance(observer, target);
    const double vertical_diff = target.altitude - observer.altitude;
    const double angle = std::atan(vertical_diff / distance);
    
    return angle > -0.1;  // Simple threshold check
}

std::vector<Geographic> TerrainCalculator::CalculateViewshed(const Geographic& observer,
                                                                          const BoundingBox2D& bounds,
                                                                          double max_distance) {
    std::vector<Geographic> visible_points;
    
    // Simplified viewshed calculation
    // Full implementation would sample the area and check line of sight
    const int samples = 10;
    const double lat_step = (bounds.max.y - bounds.min.y) / samples;
    const double lon_step = (bounds.max.x - bounds.min.x) / samples;
    
    for (int i = 0; i <= samples; ++i) {
        for (int j = 0; j <= samples; ++j) {
            const Geographic test_point(
                bounds.min.y + i * lat_step,
                bounds.min.x + j * lon_step,
                0.0
            );
            
            if (LineOfSight(observer, test_point) &&
                GeodeticCalculator::HaversineDistance(observer, test_point) <= max_distance) {
                visible_points.push_back(test_point);
            }
        }
    }
    
    return visible_points;
}

} // namespace earth_map