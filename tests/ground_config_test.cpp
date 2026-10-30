#include "ground_config.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

int main() {
  std::ifstream file("ref/raw/maps/city/meshes/city.scm", std::ios::binary);
  assert(file);
  std::string text((std::istreambuf_iterator<char>(file)), {});
  GroundConfig config;
  std::string error;
  assert(parseGroundConfig(text, 1.0f, config, error));
  assert(config.materials.size() == 108);
  assert(config.materials[0].name == "RBegin1");
  assert(config.materials[9].name == "Material");
  assert(config.materials[107].name == "ajto");
  assert(config.materials[0].rollingResistance == 0.001f);
  assert(config.materials[0].skidSound.archive == GroundResourceArchive::Sound);
  assert(parseGroundConfig("materials 2\nmaterial 0 ROAD\nmaterial 1 Road\nparams road\nfriction 0.5\nskid_PS 0xffff0023\nskid_SFX 0x1234\n", 2.0f, config, error));
  assert(config.materials[0].friction == 1.0f && config.materials[1].friction == 1.0f);
  assert(config.materials[0].skidParticles.id == 0x23);
  assert(config.materials[0].skidParticles.archive == GroundResourceArchive::Particles);
  assert(config.materials[1].skidSound.id == 0x1234 && config.materials[1].skidSoundScale == 1.0f);
  assert(!parseGroundConfig("materials -1\n", 1.0f, config, error));
  assert(!parseGroundConfig(std::string(256, 'x'), 1.0f, config, error));
  assert(!parseGroundConfig("materials 1\nmaterial 0\n", 1.0f, config, error));
}
