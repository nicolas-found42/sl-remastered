#pragma once

#include "owner_links.hpp"

#include <array>
#include <cstddef>

struct CarSfxItem {
  OwnerLink sound;
  float inversePitch = 0.0f;
  float pitchMin = 0.0f;
  float pitchMax = 0.0f;
  float volumeMin = 0.0f;
  float volumeMax = 0.0f;
};

class CarSfxTable {
public:
  static constexpr std::size_t capacity = 16;

  CarSfxTable() = default;
  ~CarSfxTable();
  CarSfxTable(const CarSfxTable&) = delete;
  CarSfxTable& operator=(const CarSfxTable&) = delete;

  std::size_t size() const { return count_; }
  float volume() const { return volume_; }
  void setVolume(float volume) { volume_ = volume; }
  const CarSfxItem* item(std::size_t index) const;
  bool addItem(LinkOwner* sound, float pitch, float pitchMin, float pitchMax,
               float volumeMin, float volumeMax);
  void clear();

private:
  std::array<CarSfxItem, capacity> items_{};
  std::size_t count_ = 0;
  float volume_ = 1.0f;
};
