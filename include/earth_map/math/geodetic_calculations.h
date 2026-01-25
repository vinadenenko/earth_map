#pragma once

/**
 * @file geodetic_calculations.h
 * @brief Geodetic calculations and utilities
 * 
 * Defines distance calculations, bearing calculations, and other
 * geodetic utilities for geographic coordinates.
 */

#include "bounding_box.h"
#include "projection.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <array>

namespace earth_map {

// Import coordinate types from coordinates namespace
using coordinates::Geographic;

/**
 * @brief Distance calculation results
 */
struct DistanceResult {
    double distance;           ///< Distance in meters
    double initial_bearing;    ///< Initial bearing in degrees
    double final_bearing;      ///< Final bearing in degrees
    
    /**
     * @brief Default constructor
     */
    constexpr DistanceResult() 
        : distance(0.0), initial_bearing(0.0), final_bearing(0.0) {}
    
    /**
     * @brief Construct from distance and bearings
     * 
     * @param dist Distance in meters
     * @param initial Initial bearing in degrees
     * @param final Final bearing in degrees
     */
    constexpr DistanceResult(double dist, double initial, double final)
        : distance(dist), initial_bearing(initial), final_bearing(final) {}
};

/**
 * @brief Geodetic calculation utilities
 */
class GeodeticCalculator {
public:
    /**
     * @brief Calculate great-circle distance using Haversine formula
     * 
     * @param point1 First geographic point
     * @param point2 Second geographic point
     * @return double Distance in meters
     */
    static double HaversineDistance(const Geographic& point1,
                                   const Geographic& point2);
    
    /**
     * @brief Calculate distance and bearings using Haversine formula
     * 
     * @param point1 First geographic point
     * @param point2 Second geographic point
     * @return DistanceResult Distance and bearing information
     */
    static DistanceResult HaversineDistanceAndBearing(const Geographic& point1,
                                                       const Geographic& point2);
    
    /**
     * @brief Calculate distance using Vincenty's formula (more accurate)
     * 
     * @param point1 First geographic point
     * @param point2 Second geographic point
     * @return double Distance in meters
     */
    static double VincentyDistance(const Geographic& point1,
                                   const Geographic& point2);
    
    /**
     * @brief Calculate distance and bearings using Vincenty's formula
     * 
     * @param point1 First geographic point
     * @param point2 Second geographic point
     * @return DistanceResult Distance and bearing information
     */
    static DistanceResult VincentyDistanceAndBearing(const Geographic& point1,
                                                    const Geographic& point2);
    
    /**
     * @brief Calculate initial bearing between two points
     * 
     * @param point1 First geographic point
     * @param point2 Second geographic point
     * @return double Initial bearing in degrees [0, 360)
     */
    static double InitialBearing(const Geographic& point1,
                                const Geographic& point2);
    
    /**
     * @brief Calculate final bearing between two points
     * 
     * @param point1 First geographic point
     * @param point2 Second geographic point
     * @return double Final bearing in degrees [0, 360)
     */
    static double FinalBearing(const Geographic& point1,
                              const Geographic& point2);
    
    /**
     * @brief Calculate destination point given start point, bearing, and distance
     * 
     * @param start Starting point
     * @param bearing Bearing in degrees [0, 360)
     * @param distance Distance in meters
     * @return Geographic Destination point
     */
    static Geographic DestinationPoint(const Geographic& start,
                                                  double bearing,
                                                  double distance);
    
    /**
     * @brief Calculate intersection point of two paths
     * 
     * @param point1 First path start point
     * @param bearing1 First path bearing in degrees
     * @param point2 Second path start point
     * @param bearing2 Second path bearing in degrees
     * @return Geographic Intersection point
     */
    static Geographic IntersectionPoint(const Geographic& point1,
                                                   double bearing1,
                                                   const Geographic& point2,
                                                   double bearing2);
    
    /**
     * @brief Calculate cross-track distance (perpendicular distance to a path)
     * 
     * @param point Point to measure from
     * @param path_start Start point of path
     * @param path_end End point of path
     * @return double Cross-track distance in meters
     */
    static double CrossTrackDistance(const Geographic& point,
                                    const Geographic& path_start,
                                    const Geographic& path_end);
    
    /**
     * @brief Calculate along-track distance (distance along path to nearest point)
     * 
     * @param point Point to measure from
     * @param path_start Start point of path
     * @param path_end End point of path
     * @return double Along-track distance in meters
     */
    static double AlongTrackDistance(const Geographic& point,
                                     const Geographic& path_start,
                                     const Geographic& path_end);
};

/**
 * @brief Geographic bounding box utilities
 */
class GeographicBounds {
public:
    /**
     * @brief Create bounding box from center point and radius
     * 
     * @param center Center geographic point
     * @param radius Radius in meters
     * @return BoundingBox2D Geographic bounding box
     */
    static BoundingBox2D FromCenterRadius(const Geographic& center, double radius);
    
    /**
     * @brief Create bounding box from multiple points
     * 
     * @param points Vector of geographic points
     * @return BoundingBox2D Geographic bounding box
     */
    static BoundingBox2D FromPoints(const std::vector<Geographic>& points);
    
    /**
     * @brief Expand bounding box by a given distance
     * 
     * @param bounds Original bounding box
     * @param distance Expansion distance in meters
     * @return BoundingBox2D Expanded bounding box
     */
    static BoundingBox2D Expand(const BoundingBox2D& bounds, double distance);
    
    /**
     * @brief Calculate area of geographic bounding box
     * 
     * @param bounds Geographic bounding box
     * @return double Area in square meters
     */
    static double CalculateArea(const BoundingBox2D& bounds);
    
    /**
     * @brief Check if two bounding boxes intersect
     * 
     * @param bounds1 First bounding box
     * @param bounds2 Second bounding box
     * @return bool True if they intersect, false otherwise
     */
    static bool Intersect(const BoundingBox2D& bounds1, const BoundingBox2D& bounds2);
    
    /**
     * @brief Merge two bounding boxes
     * 
     * @param bounds1 First bounding box
     * @param bounds2 Second bounding box
     * @return BoundingBox2D Merged bounding box
     */
    static BoundingBox2D Merge(const BoundingBox2D& bounds1, const BoundingBox2D& bounds2);
    
    /**
     * @brief Convert geographic bounding box to projected bounds
     * 
     * @param geo_bounds Geographic bounding box
     * @param projection Projection to use
     * @return BoundingBox2D Projected bounding box
     */
    static BoundingBox2D ToProjected(const BoundingBox2D& geo_bounds,
                                     const Projection& projection);
    
    /**
     * @brief Convert projected bounds to geographic bounding box
     * 
     * @param proj_bounds Projected bounding box
     * @param projection Projection that created the bounds
     * @return BoundingBox2D Geographic bounding box
     */
    static BoundingBox2D FromProjected(const BoundingBox2D& proj_bounds,
                                       const Projection& projection);
};

/**
 * @brief Geodetic path and route utilities
 */
class GeodeticPath {
public:
    /**
     * @brief Simplify a path using Douglas-Peucker algorithm
     * 
     * @param points Original path points
     * @param tolerance Tolerance in meters
     * @return std::vector<Geographic> Simplified path
     */
    static std::vector<Geographic> Simplify(const std::vector<Geographic>& points,
                                                      double tolerance);
    
    /**
     * @brief Calculate total length of a path
     * 
     * @param points Path points
     * @return double Total length in meters
     */
    static double CalculateLength(const std::vector<Geographic>& points);
    
    /**
     * @brief Interpolate point along a path at given distance
     * 
     * @param points Path points
     * @param distance Distance along path in meters
     * @return Geographic Interpolated point
     */
    static Geographic Interpolate(const std::vector<Geographic>& points,
                                            double distance);
    
    /**
     * @ Sample points along a path at regular intervals
     * 
     * @param points Path points
     * @param interval Sampling interval in meters
     * @return std::vector<Geographic> Sampled points
     */
    static std::vector<Geographic> Sample(const std::vector<Geographic>& points,
                                                     double interval);
    
    /**
     * @brief Calculate area enclosed by a polygon (signed area)
     * 
     * @param points Polygon points (should be closed)
     * @return double Signed area in square meters (positive = counter-clockwise)
     */
    static double CalculateArea(const std::vector<Geographic>& points);
    
    /**
     * @brief Calculate centroid of a polygon
     * 
     * @param points Polygon points (should be closed)
     * @return Geographic Centroid point
     */
    static Geographic CalculateCentroid(const std::vector<Geographic>& points);
    
    /**
     * @brief Check if a point is inside a polygon
     * 
     * @param point Point to test
     * @param polygon Polygon points (should be closed)
     * @return bool True if point is inside polygon, false otherwise
     */
    static bool PointInPolygon(const Geographic& point,
                                const std::vector<Geographic>& polygon);
};

/**
 * @brief Elevation and terrain calculations
 */
class TerrainCalculator {
public:
    /**
     * @brief Calculate slope between two points
     * 
     * @param point1 First point
     * @param point2 Second point
     * @return double Slope in degrees
     */
    static double CalculateSlope(const Geographic& point1,
                                const Geographic& point2);
    
    /**
     * @brief Calculate aspect (direction of steepest descent) at a point
     * 
     * @param center Center point
     * @param neighbors Eight neighboring points
     * @return double Aspect in degrees [0, 360)
     */
    static double CalculateAspect(const Geographic& center,
                                 const std::array<Geographic, 8>& neighbors);
    
    /**
     * @brief Calculate line of sight between two points
     * 
     * @param observer Observer point
     * @param target Target point
     * @param terrain_heights Terrain heights along the path (optional)
     * @return bool True if line of sight is clear, false otherwise
     */
    static bool LineOfSight(const Geographic& observer,
                           const Geographic& target,
                           const std::vector<double>& terrain_heights = {});
    
    /**
     * @brief Calculate viewshed (visible area from a point)
     * 
     * @param observer Observer point
     * @param bounds Area to analyze
     * @param max_distance Maximum viewing distance in meters
     * @return std::vector<Geographic> Visible points
     */
    static std::vector<Geographic> CalculateViewshed(const Geographic& observer,
                                                                const BoundingBox2D& bounds,
                                                                double max_distance);
};

} // namespace earth_map