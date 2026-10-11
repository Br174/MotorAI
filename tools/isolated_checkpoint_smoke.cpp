#include "motorai_core.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
int main(){
 fs::path folder="/tmp/motorai_probe_check";
 fs::remove_all(folder);
 motorai::Engine active(174);
 active.setCurriculum(3);
 active.train(2,24,0.001f);
 int step=active.globalStep(),level=active.curriculumLevel();
 if(!active.saveCheckpoint(folder.string()))return 1;
 std::fstream file(folder/"weights.bin",std::ios::binary|std::ios::in|std::ios::out);
 const char magic[8]={'M','O','T','A','I','0','0','8'};
 const char ver[4]={3,0,0,0};
 file.seekp(0);file.write(magic,8);file.write(ver,4);file.close();
 motorai::Engine probe(999);
 if(!probe.loadCheckpoint(folder.string()))return 2;
 if(probe.curriculumLevel()!=3||probe.globalStep()!=step)return 3;
 if(active.globalStep()!=step||active.curriculumLevel()!=level)return 4;
 fs::resize_file(folder/"weights.bin",30);
 motorai::Engine bad(999);
 if(bad.loadCheckpoint(folder.string()))return 5;
 fs::remove_all(folder);
 std::cout<<"PASS legacy shadow read, truncated candidate rejected, active unchanged\n";
 return 0;
}
