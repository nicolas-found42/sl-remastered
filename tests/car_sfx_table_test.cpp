#include "car_sfx_table.hpp"

#include <cassert>
#include <cmath>

int main() {
  LinkOwner sound;
  sound.value = 42;
  {
    CarSfxTable table;
    assert(table.size() == 0 && table.volume() == 1.0f && table.item(0) == nullptr);
    assert(table.addItem(&sound, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f));
    const auto* first = table.item(0);
    assert(first && first->sound.owner == &sound && first->sound.cachedValue == 42);
    assert(first->inversePitch == 0.5f && first->pitchMin == 3.0f && first->pitchMax == 4.0f);
    assert(first->volumeMin == 5.0f && first->volumeMax == 6.0f);
    assert(table.addItem(nullptr, 0.0f, 0, 0, 0, 0));
    assert(std::isinf(table.item(1)->inversePitch));
    for (std::size_t i = 2; i < CarSfxTable::capacity; ++i)
      assert(table.addItem(&sound, 1, static_cast<float>(i), 0, 0, 0));
    assert(!table.addItem(&sound, 1, 0, 0, 0, 0));
    assert(table.size() == 16 && table.item(15)->pitchMin == 15 && table.item(16) == nullptr);
    table.setVolume(0.25f);
    table.clear();
    assert(table.size() == 0 && table.volume() == 0.25f && sound.head == nullptr);
    assert(table.addItem(&sound, 4, 0, 0, 0, 0));
    assert(table.item(0)->inversePitch == 0.25f);
    setLinkOwner(*sound.head, nullptr);
    assert(table.item(0)->sound.owner == nullptr && table.size() == 1);
  }
  assert(sound.head == nullptr);
}
