#include "car_config.hpp"
#include "car_geometry.hpp"
#include "game_creation.hpp"
#include "resource_world.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <climits>
namespace fs=std::filesystem;
static void word(std::ostream& o,uint32_t x){for(int i=0;i<4;++i)o.put(static_cast<char>(x>>(8*i)));}
static void real(std::ostream& o,float f){uint32_t x;std::memcpy(&x,&f,4);word(o,x);}
int main(int argc,char**argv){
 if(argc!=3){std::cerr<<"usage: mesh_mass_corpus GAME_ROOT OUTPUT_DIR\n";return 2;}
 fs::path root=fs::canonical(argv[1]),out=argv[2];fs::create_directories(out);PackRegistry packs(root);
 std::vector<fs::path> paths;for(auto& e:fs::recursive_directory_iterator(root/"cars"))if(e.is_regular_file() && e.path().extension()==".cfg" && e.path().filename().string().rfind("_main",0)==0)paths.push_back(e.path());std::sort(paths.begin(),paths.end());
 std::ofstream bin(out/"fixtures.bin",std::ios::binary),manifest(out/"manifest.tsv");word(bin,0);manifest<<"case\tconfig\tbody\tmesh\tmass\tvertices\tfaces\n";unsigned count=0;
 for(auto& path:paths){std::string text,error;CarConfig cfg;auto rel=fs::relative(path,root).generic_string();if(!readNativeConfig(packs,rel,text,error)||!parseCarConfig(text,cfg,error)){std::cerr<<rel<<": "<<error<<'\n';return 1;}
 for(size_t i=0;i<cfg.bodies.size();++i){auto& b=cfg.bodies[i];if(b.kind!=CarBodyRecord::Body||b.shape.kind!=CarShapeKind::MeshFile||b.shape.mass==0)continue;
 std::string bytes;if(!readNativeDataFile(packs,b.shape.meshPath,INT32_MAX,bytes,error)){std::cerr<<rel<<" body "<<i<<": "<<error<<'\n';return 1;}auto scx=scxParse(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size());if(!scx.ok){std::cerr<<b.shape.meshPath<<": "<<scx.error<<'\n';return 1;}
 std::vector<physics::Vec3> vertices;std::vector<physics::MeshTriangle> faces;if(!decodeCarCollisionMesh(scx.model,vertices,faces,error)){std::cerr<<b.shape.meshPath<<": "<<error<<'\n';return 1;}
 word(bin,static_cast<uint32_t>(vertices.size()));word(bin,static_cast<uint32_t>(faces.size()));real(bin,1.0f/b.shape.mass);for(auto& v:vertices)for(float x:v)real(bin,x);for(auto& f:faces){for(auto x:f.indices)word(bin,x);for(float x:f.normal)real(bin,x);}manifest<<count+2<<'\t'<<rel<<'\t'<<i<<'\t'<<b.shape.meshPath<<'\t'<<b.shape.mass<<'\t'<<vertices.size()<<'\t'<<faces.size()<<'\n';++count;
 }}bin.seekp(0);word(bin,count);if(!bin||!manifest)return 1;std::cout<<paths.size()<<" configs, "<<count<<" dynamic mesh bodies\n";
}
