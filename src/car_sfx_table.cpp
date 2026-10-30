#include "car_sfx_table.hpp"

CarSfxTable::~CarSfxTable() { clear(); }

const CarSfxItem* CarSfxTable::item(std::size_t index) const {
  return index < count_ ? &items_[index] : nullptr;
}

bool CarSfxTable::addItem(LinkOwner* sound, float pitch, float pitchMin, float pitchMax,
                          float volumeMin, float volumeMax) {
  if (count_ == capacity) return false;
  CarSfxItem& item = items_[count_++];
  setLinkOwner(item.sound, sound);
  item.inversePitch = 1.0f / pitch;
  item.pitchMin = pitchMin;
  item.pitchMax = pitchMax;
  item.volumeMin = volumeMin;
  item.volumeMax = volumeMax;
  return true;
}

void CarSfxTable::clear() {
  for (std::size_t i = 0; i < count_; ++i) setLinkOwner(items_[i].sound, nullptr);
  count_ = 0;
}
