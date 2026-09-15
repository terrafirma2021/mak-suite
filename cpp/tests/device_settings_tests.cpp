#include "device_settings.h"
#include "makxd.h"
#include "makxd_c.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>
using namespace makxd;
int main() {
    const char* path=std::getenv("MAKXD_SETTINGS_PEER");if(!path)return 77;
    int in[2],out[2];assert(pipe(in)==0 && pipe(out)==0);auto pid=fork();assert(pid>=0);
    if(!pid){dup2(in[0],0);dup2(out[1],1);close(in[0]);close(in[1]);close(out[0]);close(out[1]);execl(path,path,nullptr);_exit(127);}
    close(in[0]);close(out[1]);FILE* writer=fdopen(in[1],"w");FILE* reader=fdopen(out[0],"r");
    detail::SettingsQuery query=[&](std::span<const uint8_t> p){for(auto b:p)fprintf(writer,"%02x",b);fputc('\n',writer);fflush(writer);
        char line[600];assert(fgets(line,sizeof(line),reader));std::vector<uint8_t> r;for(size_t i=0;line[i]!='\n';i+=2){unsigned b;assert(sscanf(line+i,"%2x",&b)==1);r.push_back(uint8_t(b));}return r;};
    auto s=detail::settingsRead(query);auto original=detail::settingsEncode(s.settings);
    s.settings.controller.buffer_ms=29;Device::controllerBehavior(s.settings,MAKXD_TUNE_RIGHT_STICK).strength_percent=71;
    s=detail::settingsApply(query,s,MAKXD_SETTINGS_CONTROLLER);assert(s.settings.controller.buffer_ms==29);
    const uint8_t count[]={0xf1},reset[]={0xf0};assert(query(count)==std::vector<uint8_t>({0,0,0,0}));
    auto file=detail::settingsExport(query,s,MAKXD_SETTINGS_CONTROLLER);assert(query(count)==std::vector<uint8_t>({0,0,0,0}));
    query(reset);assert(detail::settingsEncode(detail::settingsRead(query).settings)==original);
    s=detail::settingsImport(query,file);assert(s.settings.controller.buffer_ms==29);
    auto bad=file;bad.back()^=1;bool rejected=false;try{detail::settingsImport(query,bad);}catch(const SettingsError&){rejected=true;}assert(rejected);
    detail::settingsSave(query,s,MAKXD_SETTINGS_CONTROLLER);query(reset);assert(detail::settingsRead(query).settings.controller.buffer_ms==29);
    std::array<uint8_t,16> presetHash{};presetHash[0]=17;
    s=detail::settingsRead(query);s.settings.controller.buffer_ms=25;s=detail::settingsApply(query,s,1);
    detail::controllerPresetSave(query,presetHash,s);assert(detail::controllerPresetRead(query,presetHash).controller.buffer_ms==25);
    const uint8_t catalog[]={0x1e,0};assert(query(catalog)[5]==0);
    s.settings.controller.buffer_ms=26;s=detail::settingsApply(query,s,1);
    assert(detail::controllerPresetRead(query,presetHash).controller.buffer_ms==25);
    detail::controllerPresetSave(query,presetHash,s);query(reset);
    assert(detail::controllerPresetLoad(query,presetHash).settings.controller.buffer_ms==26);
    std::filesystem::path folder(std::getenv("MAKXD_SETTINGS_ARTIFACTS"));std::ofstream exported(folder/"cpp.makxd-settings",std::ios::binary);exported.write(reinterpret_cast<const char*>(file.data()),file.size());exported.close();
    for(const char* source:{"python","web","rust","csharp"}) {auto name=folder/(std::string(source)+".makxd-settings");if(!std::filesystem::exists(name))continue;
        std::ifstream stream(name,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),{});assert(detail::settingsImport(query,bytes).settings.controller.buffer_ms>=1);printf("CPP_IMPORT=%s\n",source);}
    /* The C ABI has the same typed representation and channel promotion. */
    auto* b=makxd_settings_controller_behavior(&s.settings,MAKXD_TUNE_LEFT_TRIGGER);assert(b && b->enabled);
    assert(makxd_settings_read(nullptr,&s)==4 && makxd_settings_controller_behavior(&s.settings,4)==nullptr);
    fclose(writer);fclose(reader);int status;waitpid(pid,&status,0);assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
    puts("CPP_SETTINGS=success live=1 export_unsaved=1 save_reboot=1 corrupt_rejected=1");
}
