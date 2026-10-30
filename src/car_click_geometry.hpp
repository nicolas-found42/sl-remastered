#pragma once

#include "car_geometry.hpp"
#include <string_view>

// 00546e10's type-9 resource payload. The resolver owns file access; the
// returned prototype owns its vertices and polygons.
bool loadCarGeometryResource(std::string_view payload, const CarCollisionMeshLoader& loadMesh,
                             CarGeometryPrototype& result, std::string& error);
// 0049a990 builds a new body sharing the resource shape, with the caller's mass.
CarGeometryPrototype cloneGeometryResource(const CarGeometryPrototype& resource, float requestedMass);

// 0046b950's file-backed click geometry (0049ab60, mass 0, type 7).
bool loadCarClickMesh(const std::string& path, const CarCollisionMeshLoader& loadMesh,
                      CarGeometryPrototype& result, std::string& error);
