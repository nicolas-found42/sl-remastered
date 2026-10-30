#pragma once

#include "ground_config.hpp"
#include "ground_parameters.hpp"
#include "road_network.hpp"

#include <filesystem>
#include <optional>
#include <string>

struct GroundFiles {
  std::optional<GroundConfig> materials;
  std::optional<RoadNetwork> roads;
};

bool prepareGroundMaterialFile(const std::filesystem::path& root, const GroundParameters& parameters,
                               float globalFriction, std::optional<GroundConfig>& out,
                               std::string& error);
bool prepareGroundRoadFile(const std::filesystem::path& root, const GroundParameters& parameters,
                           std::optional<RoadNetwork>& out, std::string& error);

// Prepares the factory's optional files under the pack registry's resource root.
// Missing files leave the corresponding value empty. Unsupported paths, I/O
// failures and data outside the parsers' supported subsets fail explicitly.
bool prepareGroundFiles(const std::filesystem::path& root, const GroundParameters& parameters,
                        float globalFriction, GroundFiles& out, std::string& error);
