# Comprehensive Production-Ready GIS System Implementation Plan

## Document Information
**Project:** Earth Map 3D GIS Core Library
**Purpose:** Transform current 35-40% complete tile renderer into production-ready GIS system
**Audience:** Development team implementing the system
**Status:** Plan Mode - Review Required
**Date:** 2026-01-20

---

## Executive Summary

### Current State Analysis

The earth_map project has achieved significant progress in foundational components:

**✅ Completed (Production-Ready)**
- Mathematical foundations (WGS84, ECEF, projections, geodetic calculations)
- Core architecture (factory patterns, RAII, clean interfaces)
- Coordinate system transformations
- Camera system with free/orbit modes, animations, and constraints
- Tile coordinate system and indexing (quadtree)
- Asynchronous tile loading with thread pool
- Two-tier tile caching (memory + disk)
- Basic OpenGL rendering pipeline
- Globe mesh generation (icosahedron-based)

**⚠️ Partially Complete**
- Tile texture rendering (texture atlas system exists but needs integration improvements)
- LOD management (basic implementation, needs adaptive camera-based subdivision)
- Performance monitoring (placeholder implementation)
- Scene management (minimal functionality)

**❌ Missing for Production GIS**
- Vector data rendering (placemarks: points, linestrings, polygons) - **CRITICAL**
- Data format parsers (KML/KMZ/GeoJSON) - **CRITICAL**
- Layer management system - **CRITICAL**
- Styling engine for features - **HIGH PRIORITY**
- Spatial analysis operations - **HIGH PRIORITY**
- Measurement tools - **MEDIUM PRIORITY**
- Ray casting/picking for interaction - **HIGH PRIORITY**
- Event system for application callbacks - **MEDIUM PRIORITY**
- Geocoding/reverse geocoding - **LOW PRIORITY**
- Advanced projections (beyond 3 current) - **LOW PRIORITY**

### Gap Analysis Summary

| Component | Current Completeness | Production Requirement | Gap |
|-----------|---------------------|------------------------|-----|
| Tile Rendering | 70% | 95% | Texture integration, adaptive LOD |
| Vector Rendering | 0% | 95% | Complete implementation needed |
| Data Parsers | 0% | 90% | KML, KMZ, GeoJSON parsers |
| Layer System | 0% | 90% | Complete layer management |
| Styling | 0% | 85% | Feature styling engine |
| Spatial Operations | 5% | 80% | Buffer, intersect, union, etc. |
| Interaction | 10% | 90% | Ray casting, picking, selection |
| Performance | 40% | 95% | GPU optimizations, profiling |
| Testing | 40% | 85% | Comprehensive test coverage |

### Recommended Implementation Strategy

This plan organizes work into **7 major phases** spanning approximately **32-40 weeks** of development effort for a small team (3-5 developers):

1. **Phase 1**: Tile System Completion (4 weeks)
2. **Phase 2**: Vector Data Foundation (6 weeks)
3. **Phase 3**: Data Format Parsers (5 weeks)
4. **Phase 4**: Layer & Styling System (6 weeks)
5. **Phase 5**: Interaction & Spatial Operations (7 weeks)
6. **Phase 6**: Performance & Optimization (6 weeks)
7. **Phase 7**: Production Polish & Documentation (6 weeks)

---

## Phase 1: Tile System Completion & Integration
**Duration:** 4 weeks
**Team Size:** 2 developers
**Priority:** CRITICAL
**Dependencies:** None (builds on existing code)

### Objectives

Complete the tile rendering pipeline to achieve seamless, high-performance textured globe visualization with proper LOD management and texture atlas integration.

### Current State

The tile system has:
- ✅ Tile coordinate mathematics
- ✅ Quadtree spatial indexing
- ✅ Async tile loading with worker threads
- ✅ Two-tier caching (memory + disk)
- ✅ Texture atlas manager
- ✅ TileTextureCoordinator with lock-free architecture
- ⚠️ Globe mesh generation (needs LOD improvements)
- ⚠️ Tile-to-globe texture mapping (integration issues exist)

### Detailed Implementation Steps

#### 1.1: Adaptive LOD Implementation (Week 1)
**Owner:** Rendering Engineer

**Context:** Current globe mesh generates fixed subdivision. Need camera-distance-based adaptive tessellation.

**Tasks:**
1. Implement screen-space error calculation
   - Calculate projected triangle edge length on screen
   - Define error threshold (e.g., 2 pixels)
   - Account for perspective foreshortening

2. Camera-distance-based subdivision
   - Calculate distance from camera to mesh triangles
   - Define subdivision distance thresholds
   - Implement recursive subdivision stopping criteria

3. Crack prevention between LOD levels
   - Implement edge matching between adjacent triangles
   - Add T-junction vertices where LOD boundaries meet
   - Test with wireframe rendering to verify seamless mesh

4. Optimize subdivision algorithm
   - Cache subdivision results for common camera positions
   - Implement incremental updates (not full rebuild)
   - Profile and optimize hot paths

**Files to Modify:**
- `src/renderer/globe_mesh.cpp` - Add adaptive subdivision
- `include/earth_map/renderer/lod_manager.h` - Enhance interface
- `src/renderer/lod_manager.cpp` - Implement LOD selection

**Acceptance Criteria:**
- [ ] Mesh detail increases smoothly as camera approaches
- [ ] No visible cracks or seams at LOD boundaries
- [ ] Maintains 60 FPS at all zoom levels (desktop)
- [ ] Screen-space error stays below 2 pixels
- [ ] Unit tests for LOD selection logic

**Testing:**
```cpp
TEST(LODManager, ScreenSpaceError) {
    // Verify error calculation at various distances
}
TEST(GlobeMesh, CrackPrevention) {
    // Verify no gaps between LOD levels
}
```

---

#### 1.2: Tile-Globe Texture Mapping Integration (Week 2)
**Owner:** Rendering Engineer + Graphics Engineer

**Context:** Texture atlas system exists but tile UV coordinates don't properly map to globe geometry.

**Tasks:**
1. Fix UV coordinate calculation for globe vertices
   - Map icosahedron vertices to geographic coordinates
   - Calculate proper UV coordinates from lat/lon
   - Handle UV seam at date line correctly
   - Test with checkerboard pattern texture

2. Integrate TileTextureCoordinator with globe rendering
   - Pass tile texture coordinates to shader
   - Implement texture atlas lookup in fragment shader
   - Handle missing tiles (fallback to parent tile texture)
   - Implement tile fade-in animations

3. Optimize texture state changes
   - Batch render calls by texture atlas
   - Minimize texture binds per frame
   - Implement texture streaming for off-screen tiles
   - Profile GPU state changes

4. Fix coordinate system transform pipeline
   - Verify consistency: Geographic → Tile Coords → UV → Texture Atlas
   - Add validation layer for coordinate transforms
   - Test edge cases (poles, date line, high zoom levels)

**Files to Modify:**
- `src/renderer/globe_mesh.cpp` - UV coordinate generation
- `src/renderer/tile_renderer.cpp` - Texture coordinate passing
- `src/shaders/globe.vert` - UV attribute handling
- `src/shaders/globe.frag` - Texture atlas sampling
- `src/data/tile_texture_coordinator.cpp` - Integration fixes

**Acceptance Criteria:**
- [ ] Tiles render correctly positioned on globe
- [ ] No texture seams or misalignment
- [ ] Smooth tile transitions during zoom
- [ ] Missing tiles show parent tile content
- [ ] Texture atlas reports <5 texture binds per frame

**Testing:**
```cpp
TEST(TileRenderer, UVMapping) {
    // Verify UV coords for known lat/lon positions
}
TEST(TileRenderer, TextureAlignment) {
    // Load test tiles, verify pixel-perfect alignment
}
```

---

#### 1.3: Tile Loading Performance Optimization (Week 3)
**Owner:** Systems Engineer

**Context:** Async loading exists but needs tuning for production performance.

**Tasks:**
1. Implement tile priority queue
   - Visible tiles get highest priority
   - Distance-based priority for off-screen tiles
   - Cancel loads for tiles that became invisible
   - Implement request deduplication

2. Optimize cache eviction strategies
   - Compare LRU vs Distance-based vs Priority-based
   - Implement hybrid eviction strategy
   - Add cache hit rate metrics
   - Tune cache sizes based on profiling

3. Add progressive tile loading
   - Load lower resolution tiles first (pyramid approach)
   - Display lower-res tiles while high-res loads
   - Implement smooth LOD transitions with alpha blending
   - Optimize memory usage during transitions

4. Network optimization
   - Implement HTTP/2 connection pooling
   - Add tile request coalescing
   - Implement retry logic with exponential backoff
   - Add bandwidth throttling for mobile

**Files to Modify:**
- `src/data/tile_loader.cpp` - Priority queue, cancellation
- `src/data/tile_cache.cpp` - Eviction strategy improvements
- `src/data/tile_manager.cpp` - Progressive loading logic
- `include/earth_map/data/tile_loader.h` - API enhancements

**Acceptance Criteria:**
- [ ] Visible tiles load within 100ms (target from plan.md)
- [ ] Cache hit rate >80% during typical navigation
- [ ] Smooth navigation with no texture pop-in
- [ ] Memory usage stays under configured limits
- [ ] Network requests batched efficiently

**Performance Benchmarks:**
```cpp
BENCHMARK(TileLoading, VisibleTiles) {
    // Measure time to load 20 visible tiles
    // Target: <100ms
}
```

---

#### 1.4: Tile System Integration Testing (Week 4)
**Owner:** QA Engineer + All Phase 1 Engineers

**Tasks:**
1. End-to-end integration tests
   - Test complete pipeline: Load → Cache → Upload → Render
   - Test multiple tile providers (OSM, Stamen, etc.)
   - Test offline mode with cached tiles
   - Test error handling (network failures, corrupted tiles)

2. Performance profiling and optimization
   - Profile CPU hotspots with perf/gprof
   - Profile GPU with RenderDoc/NSight
   - Measure memory allocations with Valgrind/Heaptrack
   - Create performance regression test suite

3. Visual testing
   - Manual testing at various zoom levels
   - Screenshot comparison tests for deterministic rendering
   - Test on different hardware (Intel/NVIDIA/AMD)
   - Test on mobile devices (if available)

4. Documentation
   - Document tile system architecture
   - Create troubleshooting guide
   - Write performance tuning guide
   - Update API documentation

**Deliverables:**
- [ ] All tile system tests passing
- [ ] Performance meets targets (60 FPS desktop, 30 FPS mobile)
- [ ] Memory usage <200MB for typical datasets
- [ ] Documentation complete and reviewed
- [ ] Phase 1 sign-off from technical lead

---

## Phase 2: Vector Data Foundation (Placemark Rendering)
**Duration:** 6 weeks
**Team Size:** 3 developers
**Priority:** CRITICAL
**Dependencies:** Phase 1 complete

### Objectives

Implement the core vector data rendering system to display points, linestrings, and polygons on the globe. This is the foundation for all GIS visualization features.

### Architecture Vision

```
┌─────────────────────────────────────────────────────┐
│              Placemark Renderer                      │
├─────────────────────────────────────────────────────┤
│  ┌───────────┐  ┌────────────┐  ┌──────────────┐   │
│  │   Point   │  │ LineString │  │   Polygon    │   │
│  │ Renderer  │  │  Renderer  │  │  Renderer    │   │
│  └───────────┘  └────────────┘  └──────────────┘   │
├─────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────┐   │
│  │        Geometry Batcher                      │   │
│  │  • Dynamic batching                          │   │
│  │  • Instanced rendering for points            │   │
│  │  • Vertex buffer management                  │   │
│  └─────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────┐   │
│  │        Feature Store                         │   │
│  │  • Geometry data structures                  │   │
│  │  • Attribute storage                         │   │
│  │  • Spatial indexing (R-tree)                 │   │
│  └─────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

### Detailed Implementation Steps

#### 2.1: Geometry Data Structures (Week 1)
**Owner:** Core Engineer

**Tasks:**
1. Implement base Geometry class hierarchy
   ```cpp
   class Geometry {
       virtual GeometryType GetType() const = 0;
       virtual BoundingBox GetBounds() const = 0;
       virtual void Accept(GeometryVisitor&) = 0;
   };

   class Point : public Geometry {
       GeographicCoordinate coordinate_;
       double altitude_ = 0.0;
   };

   class LineString : public Geometry {
       std::vector<GeographicCoordinate> coordinates_;
       bool closed_ = false;
   };

   class Polygon : public Geometry {
       LineString exterior_ring_;
       std::vector<LineString> interior_rings_;  // holes
   };

   class MultiPoint : public Geometry {
       std::vector<Point> points_;
   };

   class MultiLineString : public Geometry {
       std::vector<LineString> linestrings_;
   };

   class MultiPolygon : public Geometry {
       std::vector<Polygon> polygons_;
   };
   ```

2. Implement Feature and FeatureCollection
   ```cpp
   class Feature {
       std::unique_ptr<Geometry> geometry_;
       std::unordered_map<std::string, AttributeValue> properties_;
       std::string id_;
       Style style_;
   };

   class FeatureCollection {
       std::vector<Feature> features_;
       BoundingBox bounds_;
       // Spatial index for fast queries
       std::unique_ptr<RTree> spatial_index_;
   };
   ```

3. Implement AttributeValue variant
   ```cpp
   using AttributeValue = std::variant<
       std::string,
       double,
       int64_t,
       bool,
       std::nullptr_t
   >;
   ```

**Files to Create:**
- `include/earth_map/data/geometry.h`
- `include/earth_map/data/feature.h`
- `include/earth_map/data/feature_collection.h`
- `src/data/geometry.cpp`
- `src/data/feature.cpp`

**Acceptance Criteria:**
- [ ] All geometry types implement proper copy/move semantics
- [ ] Geometry bounds calculation correct
- [ ] Memory efficient (no unnecessary allocations)
- [ ] Comprehensive unit tests
- [ ] Documentation for all public APIs

---

#### 2.2: Point Renderer with Instanced Rendering (Week 2)
**Owner:** Graphics Engineer

**Context:** Must support millions of points at 60 FPS per project goals.

**Tasks:**
1. Implement point geometry rendering
   - Create point billboarding shader (always face camera)
   - Implement point size in pixels/meters modes
   - Support screen-space and world-space sizing
   - Implement point shape variations (circle, square, triangle)

2. Implement instanced rendering for points
   - Pack point positions into vertex buffer
   - Pack point attributes (color, size, rotation) into instance buffer
   - Use glDrawArraysInstanced for efficient rendering
   - Batch points by style for minimal draw calls

3. Implement point atlas texture support
   - Load icon images into texture atlas
   - Generate UV coordinates for icons
   - Support custom icons per feature
   - Implement icon rotation

4. Performance optimization
   - Frustum culling for point clouds
   - LOD for points (reduce count at distance)
   - GPU-based culling with compute shaders (Phase 6)
   - Benchmark with 1M+ points

**Files to Create:**
- `include/earth_map/renderer/point_renderer.h`
- `src/renderer/point_renderer.cpp`
- `src/shaders/point.vert`
- `src/shaders/point.frag`
- `src/shaders/point_instanced.vert`

**Shader Architecture:**
```glsl
// point_instanced.vert
layout(location = 0) in vec3 a_position;     // instance position
layout(location = 1) in vec4 a_color;        // instance color
layout(location = 2) in float a_size;        // instance size
layout(location = 3) in float a_rotation;    // instance rotation
layout(location = 4) in vec4 a_uv;           // instance texture UV

// Generate billboard quad in geometry shader or vertex shader
```

**Acceptance Criteria:**
- [ ] Render 100K points at 60 FPS (desktop)
- [ ] Render 1M points at 30 FPS (desktop)
- [ ] Points always face camera
- [ ] Support custom icons and colors
- [ ] Smooth performance during camera movement

**Performance Benchmarks:**
```cpp
BENCHMARK(PointRenderer, OneMillionPoints) {
    // Target: 30+ FPS on desktop, 15+ FPS on mobile
}
```

---

#### 2.3: LineString Renderer (Week 3)
**Owner:** Graphics Engineer

**Tasks:**
1. Implement line geometry generation
   - Generate triangle strips for lines with width
   - Implement screen-space line width (constant pixel width)
   - Implement world-space line width (meters)
   - Handle line end caps (butt, round, square)

2. Implement line joins
   - Miter joins for sharp angles
   - Bevel joins for performance
   - Round joins for smooth curves
   - Handle degenerate cases (zero-length segments)

3. Implement line styling
   - Solid lines
   - Dashed lines (pattern-based)
   - Dotted lines
   - Custom patterns via shader

4. Implement anti-aliasing
   - Shader-based MSAA
   - Distance field anti-aliasing for sharp lines
   - Alpha blending for smooth edges

5. Geodesic line rendering
   - Tessellate lines to follow great circles
   - Adaptive tessellation based on line length
   - Handle lines crossing date line

**Files to Create:**
- `include/earth_map/renderer/line_renderer.h`
- `src/renderer/line_renderer.cpp`
- `src/shaders/line.vert`
- `src/shaders/line.frag`
- `src/math/line_tessellation.h`
- `src/math/line_tessellation.cpp`

**Acceptance Criteria:**
- [ ] Render 10K linestrings (100 vertices each) at 60 FPS
- [ ] Smooth anti-aliased edges
- [ ] Proper geodesic rendering (no flat earth shortcuts)
- [ ] Line width consistent in screen space
- [ ] Dashed/dotted patterns work correctly

---

#### 2.4: Polygon Renderer (Week 4)
**Owner:** Graphics Engineer + Core Engineer

**Tasks:**
1. Implement polygon tessellation
   - Integrate libtess2 or implement constrained Delaunay
   - Tessellate polygons with holes
   - Handle self-intersecting polygons gracefully
   - Optimize tessellation for dynamic data

2. Implement polygon rendering
   - Render filled polygons with solid color
   - Render polygon outlines (reuse LineString renderer)
   - Support transparency and alpha blending
   - Implement depth sorting for transparent polygons

3. Implement polygon texturing
   - UV generation for polygons
   - Texture mapping with patterns
   - Support for gradient fills
   - Clipping to polygon boundaries

4. Optimize polygon rendering
   - Batch polygons by style
   - Merge adjacent polygons where possible
   - Implement polygon simplification at low zoom
   - Frustum culling for large polygon collections

**Dependencies:**
- Consider adding libtess2 dependency (GLU tessellator)
- Or implement custom tessellator (more control, more work)

**Files to Create:**
- `include/earth_map/renderer/polygon_renderer.h`
- `src/renderer/polygon_renderer.cpp`
- `src/shaders/polygon.vert`
- `src/shaders/polygon.frag`
- `include/earth_map/math/tessellation.h`
- `src/math/tessellation.cpp`

**Acceptance Criteria:**
- [ ] Render complex polygons with holes
- [ ] Handle self-intersecting polygons without crashes
- [ ] Render 1K polygons (1K vertices each) at 60 FPS
- [ ] Proper depth sorting for transparency
- [ ] Correct rendering of polygon outlines

---

#### 2.5: Placemark Renderer Integration (Week 5)
**Owner:** Rendering Lead + All Phase 2 Engineers

**Tasks:**
1. Implement PlacemarkRenderer interface
   - Consolidate Point/Line/Polygon renderers
   - Implement unified rendering pipeline
   - Handle mixed geometry types efficiently
   - Implement render order control

2. Implement geometry batching system
   - Group geometries by type and style
   - Minimize state changes and draw calls
   - Implement dynamic batching for changing data
   - Profile and optimize batch generation

3. Integrate with scene manager
   - Add placemarks to scene graph
   - Implement visibility culling
   - Handle feature selection state
   - Implement highlight rendering

4. Memory management
   - Implement vertex buffer pooling
   - Stream geometry data to GPU efficiently
   - Handle memory pressure gracefully
   - Add memory usage metrics

**Files to Modify:**
- `src/renderer/placemark_renderer.cpp` (currently returns nullptr)
- `src/core/scene_manager.cpp`
- `include/earth_map/renderer/renderer.h`

**Acceptance Criteria:**
- [ ] All geometry types render correctly
- [ ] Mixed geometries render efficiently
- [ ] Memory usage predictable and bounded
- [ ] Integration with existing rendering pipeline seamless

---

#### 2.6: Vector Rendering Testing & Benchmarking (Week 6)
**Owner:** QA Engineer + All Phase 2 Engineers

**Tasks:**
1. Comprehensive testing
   - Unit tests for each renderer
   - Integration tests for mixed geometries
   - Visual regression tests
   - Test edge cases (empty geometries, degenerate cases)

2. Performance benchmarking
   - Measure point rendering performance (10K, 100K, 1M points)
   - Measure line rendering performance
   - Measure polygon rendering performance
   - Compare against project targets (1M+ placemarks @ 30 FPS)

3. Real-world data testing
   - Test with actual KML/KMZ files (manual loading for now)
   - Test with large datasets
   - Test with complex polygons (countries, detailed boundaries)
   - Profile and identify bottlenecks

4. Documentation
   - Document rendering architecture
   - Create performance tuning guide for vector data
   - Write best practices for large datasets
   - Update API documentation

**Deliverables:**
- [ ] All vector rendering tests passing
- [ ] Performance meets targets
- [ ] Documentation complete
- [ ] Phase 2 sign-off from technical lead

---

## Phase 3: Data Format Parsers (KML, KMZ, GeoJSON)
**Duration:** 5 weeks
**Team Size:** 2-3 developers
**Priority:** CRITICAL
**Dependencies:** Phase 2 complete

### Objectives

Implement parsers for industry-standard GIS data formats, enabling the library to load real-world geographic data.

### Parser Architecture

```
┌─────────────────────────────────────────────────────┐
│           DataParser (Abstract)                      │
├─────────────────────────────────────────────────────┤
│  virtual FeatureCollection Parse(stream) = 0;       │
└─────────────────────────────────────────────────────┘
         ▲              ▲              ▲
         │              │              │
   ┌─────┴────┐   ┌────┴─────┐   ┌───┴──────┐
   │   KML    │   │  KMZ     │   │ GeoJSON  │
   │  Parser  │   │ Parser   │   │  Parser  │
   └──────────┘   └──────────┘   └──────────┘
```

### Detailed Implementation Steps

#### 3.1: KML Parser Implementation (Weeks 1-2)
**Owner:** Core Engineer 1

**Context:** KML (Keyhole Markup Language) is Google Earth's primary format.

**Tasks:**
1. Set up pugixml integration
   - Already in dependencies, start using it
   - Create XML parsing utilities
   - Handle XML namespaces correctly
   - Implement error handling for malformed XML

2. Implement KML geometry parsing
   - Parse <Point> elements → Point geometry
   - Parse <LineString> elements → LineString geometry
   - Parse <LinearRing> elements
   - Parse <Polygon> elements with inner rings → Polygon geometry
   - Parse <MultiGeometry> → Multi* geometries
   - Handle coordinate parsing (lon,lat,alt triples)

3. Implement KML styling
   - Parse <Style> and <StyleMap> elements
   - Extract IconStyle (icon, scale, heading, color)
   - Extract LineStyle (color, width)
   - Extract PolyStyle (color, fill, outline)
   - Extract LabelStyle (color, scale)
   - Implement style resolution (inline vs StyleURL references)

4. Implement KML features
   - Parse <Placemark> → Feature
   - Extract name, description as attributes
   - Parse <ExtendedData> for custom properties
   - Handle <Document> and <Folder> hierarchy
   - Implement visibility flags

5. Handle KML extensions
   - Parse camera/view elements (for future use)
   - Parse NetworkLink elements (for streaming data - Phase 6)
   - Parse <GroundOverlay> (for raster images - Phase 6)
   - Handle <ScreenOverlay> (UI elements - Phase 6)

**KML Spec Reference:** OGC KML 2.3 Specification

**Files to Create:**
- `include/earth_map/data/kml_parser.h`
- `src/data/kml_parser.cpp`
- `include/earth_map/data/kml_style.h`
- `src/data/kml_style.cpp`
- `tests/unit/test_kml_parser.cpp`
- `tests/data/sample.kml` (test fixtures)

**Acceptance Criteria:**
- [ ] Parse all basic KML geometry types
- [ ] Extract styles correctly
- [ ] Handle malformed KML gracefully
- [ ] Parse ExtendedData attributes
- [ ] Test with real-world KML files (Google Earth samples)
- [ ] Support at least 90% of KML 2.3 features

**Testing:**
```cpp
TEST(KMLParser, ParsePoint) {
    // Parse simple point placemark
}
TEST(KMLParser, ParsePolygonWithHoles) {
    // Parse complex polygon
}
TEST(KMLParser, StyleResolution) {
    // Test StyleURL references
}
```

---

#### 3.2: KMZ Parser Implementation (Week 3)
**Owner:** Core Engineer 1

**Context:** KMZ is zipped KML with embedded images/icons.

**Tasks:**
1. Set up libzip integration
   - Already in dependencies, start using it
   - Implement ZIP file reading utilities
   - Handle compressed and uncompressed entries
   - Implement error handling for corrupted archives

2. Implement KMZ extraction
   - Detect KMZ files (ZIP magic number)
   - Extract doc.kml (main document)
   - Extract icon images from files/ directory
   - Extract overlay images
   - Implement in-memory extraction (no temp files)

3. Integrate with KML parser
   - Feed extracted doc.kml to KML parser
   - Resolve icon references to extracted images
   - Load images into texture atlas
   - Handle missing resources gracefully

4. Optimize KMZ loading
   - Lazy-load embedded resources
   - Cache extracted contents
   - Stream large KMZ files
   - Implement progress callbacks

**Files to Create:**
- `include/earth_map/data/kmz_parser.h`
- `src/data/kmz_parser.cpp`
- `tests/unit/test_kmz_parser.cpp`
- `tests/data/sample.kmz` (test fixtures)

**Acceptance Criteria:**
- [ ] Extract and parse KMZ files correctly
- [ ] Load embedded icons into texture atlas
- [ ] Handle corrupted KMZ gracefully
- [ ] Memory efficient (no full extraction to disk)
- [ ] Test with real-world KMZ files

---

#### 3.3: GeoJSON Parser Implementation (Week 4)
**Owner:** Core Engineer 2

**Context:** GeoJSON is the modern web standard for geographic data.

**Tasks:**
1. Set up nlohmann/json integration
   - Already in dependencies, start using it
   - Create JSON parsing utilities
   - Implement error handling for malformed JSON
   - Handle large JSON files efficiently

2. Implement GeoJSON geometry parsing
   - Parse "Point" → Point geometry
   - Parse "LineString" → LineString geometry
   - Parse "Polygon" → Polygon geometry
   - Parse "MultiPoint", "MultiLineString", "MultiPolygon"
   - Parse "GeometryCollection"
   - Handle coordinate arrays correctly [lon, lat, alt?]

3. Implement GeoJSON features
   - Parse "Feature" objects → Feature
   - Extract "properties" → Feature attributes
   - Extract "id" field
   - Parse "FeatureCollection" → FeatureCollection
   - Handle null geometries gracefully

4. Handle coordinate reference systems
   - Default to WGS84 (EPSG:4326)
   - Parse "crs" member if present
   - Transform coordinates if needed
   - Validate coordinate ranges

5. Optimize GeoJSON parsing
   - Stream large GeoJSON files (SAX-style parsing)
   - Minimize memory allocations
   - Use move semantics for geometry construction
   - Implement progress callbacks

**GeoJSON Spec Reference:** RFC 7946

**Files to Create:**
- `include/earth_map/data/geojson_parser.h`
- `src/data/geojson_parser.cpp`
- `tests/unit/test_geojson_parser.cpp`
- `tests/data/sample.geojson` (test fixtures)

**Acceptance Criteria:**
- [ ] Parse all GeoJSON geometry types
- [ ] Extract properties correctly
- [ ] Handle malformed JSON gracefully
- [ ] Support large files (>100MB)
- [ ] Test with real-world GeoJSON files
- [ ] RFC 7946 compliant

**Testing:**
```cpp
TEST(GeoJSONParser, ParseFeatureCollection) {
    // Parse collection with multiple features
}
TEST(GeoJSONParser, HandleNullGeometry) {
    // Test null geometry handling
}
```

---

#### 3.4: Parser Factory & Integration (Week 5)
**Owner:** Core Engineer 3

**Tasks:**
1. Implement DataParser factory
   - Auto-detect file format (extension + magic numbers)
   - Route to appropriate parser
   - Implement parser registration system
   - Handle unknown formats gracefully

2. Integrate with EarthMap::LoadData()
   - Implement the currently stubbed LoadData() method
   - Load file into appropriate parser
   - Create FeatureCollection from parse result
   - Add features to scene
   - Return success/failure status

3. Implement async loading
   - Load files on background thread
   - Provide progress callbacks
   - Handle cancellation
   - Thread-safe integration with renderer

4. Error handling and validation
   - Validate parsed geometries (no NaN, valid coordinates)
   - Log warnings for unsupported features
   - Provide detailed error messages
   - Implement data validation layer

5. Testing and documentation
   - Integration tests with all formats
   - Test large files (>100MB)
   - Test malformed files
   - Document supported formats and limitations

**Files to Modify:**
- `src/core/scene_manager.cpp` - Implement LoadData()
- `src/core/earth_map_impl.cpp` - Wire up loading pipeline

**Files to Create:**
- `include/earth_map/data/parser_factory.h`
- `src/data/parser_factory.cpp`

**Acceptance Criteria:**
- [ ] Auto-detect and parse KML, KMZ, GeoJSON
- [ ] LoadData() fully functional
- [ ] Async loading works correctly
- [ ] Comprehensive error handling
- [ ] Documentation complete

**Deliverables:**
- [ ] All parsers complete and tested
- [ ] LoadData() functional
- [ ] Phase 3 sign-off from technical lead

---

## Phase 4: Layer Management & Styling System
**Duration:** 6 weeks
**Team Size:** 2 developers
**Priority:** HIGH
**Dependencies:** Phases 2 & 3 complete

### Objectives

Implement a layer management system for organizing and controlling the display of multiple data sources, plus a flexible styling engine for customizing feature appearance.

### System Architecture

```
┌─────────────────────────────────────────────────────┐
│              Layer Manager                           │
├─────────────────────────────────────────────────────┤
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │ Layer 1  │  │ Layer 2  │  │ Layer 3  │  ...     │
│  │ (Vector) │  │ (Raster) │  │ (Vector) │          │
│  └──────────┘  └──────────┘  └──────────┘          │
│                                                      │
│  Layer Properties:                                   │
│  • Visibility, Opacity, Z-Index                      │
│  • Bounding Box, Metadata                            │
│  • Style (layer-level default)                       │
└─────────────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────────────┐
│              Style Engine                            │
├─────────────────────────────────────────────────────┤
│  • Rule-based styling                                │
│  • Property-based styling (data-driven)              │
│  • Style inheritance                                 │
│  • Dynamic style evaluation                          │
└─────────────────────────────────────────────────────┘
```

### Detailed Implementation Steps

#### 4.1: Layer Data Model (Week 1)
**Owner:** Core Engineer

**Tasks:**
1. Implement Layer base class
   ```cpp
   class Layer {
   public:
       enum class Type { VECTOR, RASTER, TILE };

       std::string id_;
       std::string name_;
       bool visible_ = true;
       float opacity_ = 1.0f;
       int z_index_ = 0;
       BoundingBox bounds_;
       std::unordered_map<std::string, std::string> metadata_;

       virtual Type GetType() const = 0;
       virtual void Render(const RenderContext&) = 0;
       virtual FeatureCollection* GetFeatures() = 0;  // vector only
   };
   ```

2. Implement VectorLayer
   ```cpp
   class VectorLayer : public Layer {
       FeatureCollection features_;
       Style default_style_;
       std::unique_ptr<StyleEngine> style_engine_;
       std::unique_ptr<RTree> spatial_index_;

       Type GetType() const override { return Type::VECTOR; }
       void Render(const RenderContext&) override;
       void AddFeature(Feature feature);
       void RemoveFeature(const std::string& id);
       std::vector<Feature*> QueryFeatures(const BoundingBox& bounds);
   };
   ```

3. Implement TileLayer (wrapper around existing tile system)
   ```cpp
   class TileLayer : public Layer {
       TileProvider provider_;
       int min_zoom_ = 0;
       int max_zoom_ = 18;

       Type GetType() const override { return Type::TILE; }
       void Render(const RenderContext&) override;
   };
   ```

4. Implement LayerGroup (for hierarchical organization)
   ```cpp
   class LayerGroup : public Layer {
       std::vector<std::unique_ptr<Layer>> children_;

       void AddLayer(std::unique_ptr<Layer> layer);
       Layer* GetLayer(const std::string& id);
       void RemoveLayer(const std::string& id);
   };
   ```

**Files to Create:**
- `include/earth_map/data/layer.h`
- `include/earth_map/data/vector_layer.h`
- `include/earth_map/data/tile_layer.h`
- `include/earth_map/data/layer_group.h`
- `src/data/layer.cpp`
- `src/data/vector_layer.cpp`

**Acceptance Criteria:**
- [ ] Layer hierarchy working correctly
- [ ] Visibility and opacity control functional
- [ ] Z-index ordering correct
- [ ] Clean ownership model (RAII)
- [ ] Unit tests for layer operations

---

#### 4.2: Layer Manager Implementation (Week 2)
**Owner:** Core Engineer

**Tasks:**
1. Implement LayerManager
   ```cpp
   class LayerManager {
   public:
       void AddLayer(std::unique_ptr<Layer> layer);
       Layer* GetLayer(const std::string& id);
       void RemoveLayer(const std::string& id);
       void SetLayerVisibility(const std::string& id, bool visible);
       void SetLayerOpacity(const std::string& id, float opacity);
       void SetLayerZIndex(const std::string& id, int z_index);
       void MoveLayerUp(const std::string& id);
       void MoveLayerDown(const std::string& id);

       std::vector<Layer*> GetVisibleLayers() const;
       std::vector<Layer*> GetLayersInRenderOrder() const;

   private:
       std::unordered_map<std::string, std::unique_ptr<Layer>> layers_;
       std::vector<std::string> render_order_;  // sorted by z-index
   };
   ```

2. Integrate with scene manager
   - Replace flat feature collection with layer manager
   - Update rendering pipeline to iterate layers
   - Implement layer-based culling
   - Handle layer opacity in rendering

3. Implement layer persistence
   - Serialize layer configuration to JSON
   - Deserialize layer configuration
   - Save/load layer state

4. Add layer events
   - LayerAdded, LayerRemoved
   - LayerVisibilityChanged
   - LayerOrderChanged
   - Wire to event system (Phase 5)

**Files to Create:**
- `include/earth_map/data/layer_manager.h`
- `src/data/layer_manager.cpp`

**Files to Modify:**
- `src/core/scene_manager.cpp`
- `src/renderer/renderer.cpp`

**Acceptance Criteria:**
- [ ] Layer CRUD operations working
- [ ] Layer ordering correct
- [ ] Layer visibility affects rendering
- [ ] Layer opacity applied correctly
- [ ] Integration tests pass

---

#### 4.3: Style Data Model (Week 3)
**Owner:** Graphics Engineer

**Tasks:**
1. Implement Style classes
   ```cpp
   struct Color {
       uint8_t r, g, b, a;
       static Color FromHex(const std::string& hex);
   };

   struct PointStyle {
       Color fill_color = {255, 0, 0, 255};
       Color stroke_color = {0, 0, 0, 255};
       float stroke_width = 1.0f;
       float size = 10.0f;  // pixels or meters
       SizeMode size_mode = SizeMode::PIXELS;
       std::string icon_url;
       float icon_scale = 1.0f;
       float rotation = 0.0f;  // degrees
   };

   struct LineStyle {
       Color stroke_color = {0, 0, 255, 255};
       float stroke_width = 2.0f;
       LineCapStyle cap = LineCapStyle::BUTT;
       LineJoinStyle join = LineJoinStyle::MITER;
       std::vector<float> dash_pattern;  // e.g., {10, 5} = 10px dash, 5px gap
   };

   struct PolygonStyle {
       Color fill_color = {0, 255, 0, 128};
       Color stroke_color = {0, 0, 0, 255};
       float stroke_width = 1.0f;
       bool fill = true;
       bool stroke = true;
       std::string pattern_url;  // texture pattern
   };

   struct LabelStyle {
       std::string text_field;  // property name or template
       std::string font_family = "Arial";
       float font_size = 12.0f;
       Color text_color = {0, 0, 0, 255};
       Color halo_color = {255, 255, 255, 255};
       float halo_width = 1.0f;
       TextAnchor anchor = TextAnchor::CENTER;
       glm::vec2 offset = {0, 0};
   };

   class Style {
   public:
       std::optional<PointStyle> point_style;
       std::optional<LineStyle> line_style;
       std::optional<PolygonStyle> polygon_style;
       std::optional<LabelStyle> label_style;

       // Get appropriate style for geometry type
       const PointStyle* GetPointStyle() const;
       const LineStyle* GetLineStyle() const;
       const PolygonStyle* GetPolygonStyle() const;
   };
   ```

2. Implement style inheritance
   - Default style (system-wide)
   - Layer style (overrides default)
   - Feature style (overrides layer)
   - Implement merge logic

3. Support style from KML/GeoJSON
   - Convert KML styles to internal format
   - Convert GeoJSON paint properties
   - Handle style URLs and references

**Files to Create:**
- `include/earth_map/renderer/style.h`
- `src/renderer/style.cpp`

**Acceptance Criteria:**
- [ ] All style types defined
- [ ] Style inheritance working
- [ ] KML/GeoJSON style conversion correct
- [ ] Unit tests for style operations

---

#### 4.4: Style Engine Implementation (Week 4)
**Owner:** Graphics Engineer

**Tasks:**
1. Implement rule-based styling
   ```cpp
   class StyleRule {
   public:
       // Filter: only apply to features matching condition
       std::function<bool(const Feature&)> filter;

       // Style to apply when filter matches
       Style style;

       // Priority (higher = evaluated later, can override)
       int priority = 0;
   };

   class StyleEngine {
   public:
       void AddRule(StyleRule rule);
       void RemoveRule(int rule_id);

       // Evaluate all rules and return final style for feature
       Style EvaluateStyle(const Feature& feature) const;

   private:
       std::vector<StyleRule> rules_;
       Style default_style_;
   };
   ```

2. Implement property-based styling (data-driven)
   ```cpp
   // Example: color based on population property
   // "fill_color": ["interpolate", ["linear"], ["get", "population"],
   //                0, "#fee5d9", 100000, "#de2d26"]

   class StyleExpression {
   public:
       virtual AttributeValue Evaluate(const Feature& feature) const = 0;
   };

   class InterpolateExpression : public StyleExpression {
       // Interpolate between values based on property
   };
   ```

3. Implement style caching
   - Cache evaluated styles per feature
   - Invalidate cache when rules change
   - Optimize for static data

4. Integrate with renderers
   - Update PointRenderer to use evaluated styles
   - Update LineRenderer to use evaluated styles
   - Update PolygonRenderer to use evaluated styles
   - Support dynamic style updates

**Files to Create:**
- `include/earth_map/renderer/style_engine.h`
- `include/earth_map/renderer/style_expression.h`
- `src/renderer/style_engine.cpp`
- `src/renderer/style_expression.cpp`

**Acceptance Criteria:**
- [ ] Rule-based styling functional
- [ ] Property-based styling functional
- [ ] Style caching working
- [ ] Renderer integration complete

---

#### 4.5: Label Rendering System (Week 5)
**Owner:** Graphics Engineer + Core Engineer

**Tasks:**
1. Implement text rendering
   - Integrate FreeType for font rasterization
   - Create text texture atlas
   - Implement glyph rendering
   - Support multiple fonts

2. Implement label placement
   - Point labels (8 positions around point)
   - Line labels (follow line path)
   - Polygon labels (centroid or largest area)
   - Handle label rotation

3. Implement label collision detection
   - Build spatial index of label bounding boxes
   - Detect overlapping labels
   - Implement priority-based culling
   - Support label decluttering

4. Label styling
   - Apply LabelStyle (font, size, color, halo)
   - Support multi-line labels
   - Text truncation and ellipsis
   - Background boxes (optional)

**Dependencies:**
- Add FreeType dependency to conanfile.py

**Files to Create:**
- `include/earth_map/renderer/label_renderer.h`
- `include/earth_map/renderer/text_atlas.h`
- `src/renderer/label_renderer.cpp`
- `src/renderer/text_atlas.cpp`
- `src/shaders/text.vert`
- `src/shaders/text.frag`

**Acceptance Criteria:**
- [ ] Render text labels on features
- [ ] Collision detection prevents overlap
- [ ] Label styling works correctly
- [ ] Performance acceptable (1K+ labels @ 60 FPS)

---

#### 4.6: Layer & Style System Testing (Week 6)
**Owner:** QA Engineer + All Phase 4 Engineers

**Tasks:**
1. Comprehensive testing
   - Layer CRUD operations
   - Layer ordering and visibility
   - Style evaluation correctness
   - Label rendering and collision detection

2. Integration testing
   - Load KML with layers and styles
   - Load GeoJSON with styling
   - Test complex style rules
   - Test with real-world datasets

3. Performance testing
   - Measure layer switching overhead
   - Measure style evaluation performance
   - Optimize hot paths

4. Documentation
   - Layer API documentation
   - Style engine guide
   - Styling examples
   - Best practices

**Deliverables:**
- [ ] All tests passing
- [ ] Documentation complete
- [ ] Phase 4 sign-off from technical lead

---

## Phase 5: Interaction & Spatial Operations
**Duration:** 7 weeks
**Team Size:** 3 developers
**Priority:** HIGH
**Dependencies:** Phases 2, 3, 4 complete

### Objectives

Implement interactive features (picking, selection, measurement) and spatial analysis operations (buffer, intersection, etc.) to enable application-level GIS functionality.

### System Architecture

```
┌─────────────────────────────────────────────────────┐
│           Interaction System                         │
├─────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐                 │
│  │ Ray Caster   │  │ Feature      │                 │
│  │ (Screen →    │  │ Picker       │                 │
│  │  Geographic) │  │ (Selection)  │                 │
│  └──────────────┘  └──────────────┘                 │
├─────────────────────────────────────────────────────┤
│           Spatial Analysis                           │
├─────────────────────────────────────────────────────┤
│  • Buffer      • Intersection  • Union               │
│  • Difference  • Distance      • Contains            │
│  • Within      • Crosses       • Overlaps            │
└─────────────────────────────────────────────────────┘
```

### Detailed Implementation Steps

#### 5.1: Ray Casting System (Week 1)
**Owner:** Math Engineer

**Tasks:**
1. Implement ray-sphere intersection
   - Screen coordinates → world ray
   - Ray-ellipsoid intersection (WGS84)
   - Handle multiple intersection points (front/back)
   - Return geographic coordinates

2. Implement ray-terrain intersection
   - Integrate with height map data
   - Binary search along ray for terrain hit
   - Interpolate height values
   - Return precise altitude

3. Optimize ray casting
   - Cache ray results for static camera
   - Use bounding volumes for early rejection
   - Implement LOD-based precision
   - Profile and optimize

**Files to Create:**
- `include/earth_map/math/ray.h`
- `include/earth_map/math/ray_casting.h`
- `src/math/ray.cpp`
- `src/math/ray_casting.cpp`

**Acceptance Criteria:**
- [ ] Screen-to-geographic conversion accurate
- [ ] Handles edge cases (poles, date line)
- [ ] Works with terrain elevation
- [ ] Performance <1ms per ray cast
- [ ] Unit tests with known coordinates

---

#### 5.2: Feature Picking System (Weeks 2-3)
**Owner:** Graphics Engineer + Core Engineer

**Tasks:**
1. Implement spatial query system
   - Query features within radius of point
   - Query features within bounding box
   - Use spatial index (R-tree) for fast lookup
   - Return results sorted by distance

2. Implement geometry hit testing
   - Point-in-polygon test
   - Point-to-linestring distance
   - Point-to-point distance
   - Threshold-based selection (e.g., 10 pixel tolerance)

3. Implement screen-space picking
   - Convert click position to geographic coordinate
   - Query features near coordinate
   - Test each feature for intersection
   - Return closest feature

4. Implement selection state management
   - Selected features list
   - Highlight rendering for selected features
   - Multi-select support (Ctrl+click)
   - Clear selection

5. Implement hover detection
   - Track mouse position
   - Detect features under cursor
   - Show hover highlights
   - Update cursor style

**Files to Create:**
- `include/earth_map/data/spatial_query.h`
- `include/earth_map/interaction/picker.h`
- `include/earth_map/interaction/selection_manager.h`
- `src/data/spatial_query.cpp`
- `src/interaction/picker.cpp`
- `src/interaction/selection_manager.cpp`

**Acceptance Criteria:**
- [ ] Click selects nearest feature
- [ ] Selection highlighting visible
- [ ] Multi-select works correctly
- [ ] Hover detection functional
- [ ] Performance acceptable (pick in <10ms)

---

#### 5.3: Measurement Tools (Week 4)
**Owner:** Core Engineer

**Tasks:**
1. Implement distance measurement
   - Measure distance between two points
   - Measure path length (multiple points)
   - Use geodesic calculations (Vincenty)
   - Display distance in appropriate units (m, km, mi)

2. Implement area measurement
   - Measure polygon area
   - Use spherical excess algorithm for accuracy
   - Display area in appropriate units (m², km², acres)
   - Handle polygons with holes

3. Implement measurement UI helpers
   - Draw measurement lines/polygons on map
   - Display measurement labels
   - Interactive measurement mode
   - Clear measurements

**Files to Create:**
- `include/earth_map/interaction/measurement_tool.h`
- `src/interaction/measurement_tool.cpp`

**Acceptance Criteria:**
- [ ] Distance measurements accurate (within 0.1%)
- [ ] Area measurements accurate
- [ ] Interactive measurement functional
- [ ] UI clear and intuitive

---

#### 5.4: Spatial Analysis Foundation (Week 5)
**Owner:** GIS Engineer

**Tasks:**
1. Integrate GEOS library
   - Add GEOS dependency (Geometry Engine - Open Source)
   - Create GEOS wrapper utilities
   - Convert internal geometries to/from GEOS
   - Handle GEOS exceptions

2. Implement basic spatial operations
   ```cpp
   class SpatialOperations {
   public:
       // Geometric operations
       static std::unique_ptr<Geometry> Buffer(
           const Geometry& geom, double distance);
       static std::unique_ptr<Geometry> Intersection(
           const Geometry& a, const Geometry& b);
       static std::unique_ptr<Geometry> Union(
           const Geometry& a, const Geometry& b);
       static std::unique_ptr<Geometry> Difference(
           const Geometry& a, const Geometry& b);
       static std::unique_ptr<Geometry> SymmetricDifference(
           const Geometry& a, const Geometry& b);

       // Spatial predicates
       static bool Contains(const Geometry& a, const Geometry& b);
       static bool Within(const Geometry& a, const Geometry& b);
       static bool Intersects(const Geometry& a, const Geometry& b);
       static bool Crosses(const Geometry& a, const Geometry& b);
       static bool Overlaps(const Geometry& a, const Geometry& b);
       static bool Touches(const Geometry& a, const Geometry& b);
       static bool Disjoint(const Geometry& a, const Geometry& b);

       // Measurements
       static double Distance(const Geometry& a, const Geometry& b);
       static double Area(const Geometry& geom);
       static double Length(const Geometry& geom);
       static Point Centroid(const Geometry& geom);
   };
   ```

3. Implement geometry validation
   - Check for valid geometries
   - Fix invalid geometries (self-intersections, etc.)
   - Simplify complex geometries

**Dependencies:**
- Add GEOS 3.11+ to conanfile.py

**Files to Create:**
- `include/earth_map/data/spatial_operations.h`
- `src/data/spatial_operations.cpp`
- `include/earth_map/data/geos_wrapper.h`
- `src/data/geos_wrapper.cpp`

**Acceptance Criteria:**
- [ ] All spatial operations functional
- [ ] GEOS integration clean
- [ ] Handles invalid geometries gracefully
- [ ] Unit tests for each operation

---

#### 5.5: Event System Implementation (Week 6)
**Owner:** Core Engineer

**Tasks:**
1. Implement event types
   ```cpp
   enum class EventType {
       // Data events
       DATA_LOADED,
       DATA_LOADING_PROGRESS,
       DATA_LOADING_ERROR,

       // Tile events
       TILES_LOADING,
       TILES_LOADED,
       TILE_ERROR,

       // Camera events
       CAMERA_MOVE_START,
       CAMERA_MOVE,
       CAMERA_MOVE_END,

       // Layer events
       LAYER_ADDED,
       LAYER_REMOVED,
       LAYER_VISIBILITY_CHANGED,
       LAYER_ORDER_CHANGED,

       // Feature events
       FEATURE_CLICK,
       FEATURE_HOVER,
       FEATURE_SELECTED,
       FEATURE_DESELECTED,

       // Rendering events
       RENDER_FRAME_START,
       RENDER_FRAME_END,

       // Error events
       ERROR
   };

   struct Event {
       EventType type;
       std::unordered_map<std::string, std::string> data;
       std::chrono::system_clock::time_point timestamp;
   };
   ```

2. Implement event dispatcher
   ```cpp
   class EventDispatcher {
   public:
       using EventCallback = std::function<void(const Event&)>;

       // Register callback for event type
       int Subscribe(EventType type, EventCallback callback);

       // Unregister callback
       void Unsubscribe(int subscription_id);

       // Emit event to all subscribers
       void Dispatch(const Event& event);

   private:
       std::unordered_map<EventType,
           std::vector<std::pair<int, EventCallback>>> subscribers_;
       int next_id_ = 0;
   };
   ```

3. Integrate event system
   - Add event dispatcher to EarthMap
   - Emit events from all components
   - Provide event subscription API
   - Document all event types

**Files to Create:**
- `include/earth_map/core/event.h`
- `include/earth_map/core/event_dispatcher.h`
- `src/core/event_dispatcher.cpp`

**Files to Modify:**
- `include/earth_map/earth_map.h` - Add event subscription methods
- All components - Emit events

**Acceptance Criteria:**
- [ ] All event types defined
- [ ] Event subscription working
- [ ] Events emitted from all components
- [ ] No memory leaks in event system
- [ ] Thread-safe event dispatching

---

#### 5.6: Interaction System Testing (Week 7)
**Owner:** QA Engineer + All Phase 5 Engineers

**Tasks:**
1. Comprehensive testing
   - Ray casting accuracy tests
   - Feature picking tests
   - Measurement tool accuracy tests
   - Spatial operation correctness tests
   - Event system tests

2. Integration testing
   - Test complete interaction workflows
   - Test with complex geometries
   - Test with large datasets
   - Profile performance

3. User testing
   - Manual testing of interaction features
   - Usability testing
   - Gather feedback

4. Documentation
   - Interaction API documentation
   - Spatial operations guide
   - Event system documentation
   - Example code

**Deliverables:**
- [ ] All tests passing
- [ ] Performance acceptable
- [ ] Documentation complete
- [ ] Phase 5 sign-off from technical lead

---

## Phase 6: Performance Optimization & Production Features
**Duration:** 6 weeks
**Team Size:** 3 developers
**Priority:** MEDIUM
**Dependencies:** Phases 1-5 complete

### Objectives

Optimize performance to meet project targets (1M+ placemarks @ 30+ FPS), implement advanced GPU features, and add production-ready capabilities.

### Focus Areas

1. **GPU Optimizations**
   - Compute shaders for data processing
   - GPU-based culling and LOD
   - Texture compression
   - Buffer optimization

2. **Memory Management**
   - Smart caching strategies
   - Memory pooling
   - Streaming optimizations
   - Garbage collection

3. **Threading**
   - Multi-threaded data loading
   - Parallel geometry processing
   - Async spatial operations

4. **Production Features**
   - Performance monitoring
   - Settings management (low/medium/high)
   - Screenshot/export functionality
   - Crash reporting

### Detailed Steps

*(Due to length, providing high-level breakdown. Each item would be 1-2 weeks)*

#### 6.1: GPU Optimization (Weeks 1-2)
- Implement compute shaders for frustum culling
- Implement compute shaders for LOD selection
- Optimize vertex/index buffer usage
- Implement texture compression (BC7, ASTC)
- GPU-based point clustering

#### 6.2: Memory Optimization (Week 3)
- Implement memory pooling for geometries
- Optimize cache eviction strategies
- Implement streaming for large datasets
- Memory pressure handling
- Profile memory usage

#### 6.3: Multi-Threading (Week 4)
- Extend tile loading worker pool
- Parallel geometry tessellation
- Async spatial operations
- Thread-safe data structures
- Minimize render thread blocking

#### 6.4: Performance Monitoring (Week 5)
- Real-time FPS display
- GPU/CPU memory usage tracking
- Frame time breakdown (CPU/GPU)
- Network statistics (tile loading)
- Render statistics (draw calls, vertices)
- Performance graph visualization

#### 6.5: Settings & Quality Presets (Week 6)
- Low/Medium/High quality presets
- Configurable tile cache sizes
- LOD distance thresholds
- Max features rendered
- Texture quality settings
- Save/load settings

---

## Phase 7: Production Polish & Documentation
**Duration:** 6 weeks
**Team Size:** 3-4 developers
**Priority:** HIGH
**Dependencies:** All previous phases

### Objectives

Finalize the library for production use with comprehensive testing, documentation, examples, and deployment preparation.

### Detailed Steps

#### 7.1: Comprehensive Testing (Weeks 1-2)
- Increase test coverage to >85%
- Integration test suite
- Performance regression tests
- Memory leak detection (Valgrind)
- Stress testing with large datasets
- Cross-platform testing (Windows/Linux/Mac)

#### 7.2: API Refinement (Week 3)
- API review and polish
- Consistent naming conventions
- Error handling review
- Deprecate legacy APIs
- Version 1.0 API freeze

#### 7.3: Documentation (Week 4)
- Complete API reference (Doxygen)
- Architecture documentation
- User guides and tutorials
- Performance tuning guide
- Troubleshooting guide
- Contribution guidelines

#### 7.4: Example Applications (Week 5)
- Basic example (already exists, polish)
- KML/KMZ viewer example
- GeoJSON viewer example
- Interactive measurement tool example
- Custom styling example
- Large dataset example (1M+ points)

#### 7.5: Deployment Preparation (Week 6)
- CMake install targets
- Package configuration (pkg-config, CMake config)
- Conan package recipe
- GitHub releases
- CI/CD pipeline (GitHub Actions)
- Performance benchmarks in CI

---

## Critical Files to Create/Modify

### Phase 1 (Tile System)
- `src/renderer/globe_mesh.cpp` - LOD implementation
- `src/renderer/tile_renderer.cpp` - Texture integration
- `src/data/tile_loader.cpp` - Priority queue

### Phase 2 (Vector Rendering)
- `include/earth_map/data/geometry.h` - NEW
- `include/earth_map/renderer/point_renderer.h` - NEW
- `include/earth_map/renderer/line_renderer.h` - NEW
- `include/earth_map/renderer/polygon_renderer.h` - NEW
- `src/renderer/placemark_renderer.cpp` - Implement stub

### Phase 3 (Parsers)
- `include/earth_map/data/kml_parser.h` - NEW
- `include/earth_map/data/kmz_parser.h` - NEW
- `include/earth_map/data/geojson_parser.h` - NEW
- `src/core/scene_manager.cpp` - Implement LoadData()

### Phase 4 (Layers & Styling)
- `include/earth_map/data/layer.h` - NEW
- `include/earth_map/data/layer_manager.h` - NEW
- `include/earth_map/renderer/style.h` - NEW
- `include/earth_map/renderer/style_engine.h` - NEW

### Phase 5 (Interaction)
- `include/earth_map/math/ray_casting.h` - NEW
- `include/earth_map/interaction/picker.h` - NEW
- `include/earth_map/data/spatial_operations.h` - NEW
- `include/earth_map/core/event.h` - NEW

### Phase 6 (Optimization)
- New compute shaders
- Performance monitoring system
- Settings manager

### Phase 7 (Polish)
- Documentation files
- Example applications
- CMake install targets

---

## Team Organization Recommendations

### Team Structure (3-5 people)

1. **Technical Lead** (1 person)
   - Overall architecture decisions
   - Code review
   - Phase sign-offs
   - Risk management

2. **Core/GIS Engineers** (2 people)
   - Data structures (geometries, features)
   - Parsers (KML, GeoJSON)
   - Spatial operations
   - Layer management

3. **Graphics/Rendering Engineers** (1-2 people)
   - Vector rendering (points, lines, polygons)
   - Shaders
   - Styling engine
   - Performance optimization

4. **QA/Systems Engineer** (1 person)
   - Testing
   - Build system
   - CI/CD
   - Performance benchmarking

### Parallel Work Streams

- **Stream 1** (Rendering): Phase 1 → Phase 2 → Phase 4 (styling)
- **Stream 2** (Data): Phase 3 → Phase 4 (layers) → Phase 5 (spatial ops)
- **Stream 3** (QA): Testing throughout, leads Phase 7

### Weekly Cadence

- **Monday**: Sprint planning, task assignment
- **Wednesday**: Mid-week sync, blocker resolution
- **Friday**: Demo, code review, retrospective

---

## Risk Assessment & Mitigation

### Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Performance targets not met | Medium | High | Early benchmarking, iterative optimization, fallback quality settings |
| GEOS integration issues | Low | Medium | Evaluate alternatives (Boost.Geometry), maintain fallback implementations |
| Texture memory exhaustion | Medium | High | Aggressive eviction, texture compression, configurable limits |
| Parser incompatibilities | Medium | Low | Comprehensive test suites with real-world data, gradual feature support |
| Threading bugs | Medium | Medium | Extensive testing, ThreadSanitizer, minimize shared state |

### Schedule Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Scope creep | High | High | Strict MVP definition, backlog prioritization, phase gates |
| Underestimation | Medium | Medium | 20% buffer in estimates, regular velocity tracking |
| Dependencies on external libraries | Low | Medium | Early integration, fallback implementations |
| Key personnel unavailable | Low | High | Knowledge sharing, documentation, pair programming |

---

## Success Criteria

### Functional Requirements
- [ ] Render textured globe with seamless tiles
- [ ] Render 1M+ points at 30+ FPS (desktop)
- [ ] Load and display KML, KMZ, GeoJSON files
- [ ] Layer management with visibility/ordering
- [ ] Feature styling (points, lines, polygons, labels)
- [ ] Interactive feature selection
- [ ] Spatial analysis operations (buffer, intersection, etc.)
- [ ] Measurement tools (distance, area)
- [ ] Event system for application integration
- [ ] Performance monitoring

### Performance Requirements
- [ ] 60+ FPS desktop, 30+ FPS mobile (typical scenes)
- [ ] 1M+ placemarks at 30+ FPS
- [ ] Tile loading <100ms
- [ ] Memory usage <200MB (typical datasets)
- [ ] Startup time <2 seconds

### Quality Requirements
- [ ] Test coverage >85%
- [ ] Zero memory leaks (Valgrind clean)
- [ ] Zero crashes in 100 hours automated testing
- [ ] All public APIs documented
- [ ] Architecture documented
- [ ] Example applications working

### Production Readiness
- [ ] API stable (version 1.0)
- [ ] CMake package config
- [ ] CI/CD pipeline
- [ ] Performance benchmarks in CI
- [ ] Cross-platform tested (Win/Linux/Mac)
- [ ] Error handling comprehensive
- [ ] Logging throughout

---

## Conclusion

This comprehensive plan transforms the earth_map project from a 35-40% complete tile renderer into a production-ready, full-featured GIS core library. The phased approach ensures manageable development while maintaining architectural integrity.

**Total Estimated Effort:** 32-40 weeks with 3-5 developers

**Key Priorities:**
1. Complete tile system (4 weeks)
2. Implement vector rendering (6 weeks)
3. Add data parsers (5 weeks)
4. Build layer & styling system (6 weeks)
5. Enable interaction (7 weeks)
6. Optimize performance (6 weeks)
7. Polish for production (6 weeks)

Each phase builds upon the previous, with clear acceptance criteria and testing requirements. The plan balances ambitious functionality with practical constraints, creating a foundation that can compete with established GIS systems.

---

## Next Steps

1. **Review this plan** with technical lead and stakeholders
2. **Prioritize phases** based on business needs
3. **Allocate team members** to work streams
4. **Set up project management** (Jira, GitHub Projects, etc.)
5. **Begin Phase 1** with tile system completion
6. **Establish CI/CD pipeline** for continuous integration
7. **Regular performance benchmarking** against targets

This plan provides a clear roadmap for creating a world-class 3D GIS rendering library suitable for production use in demanding applications.
