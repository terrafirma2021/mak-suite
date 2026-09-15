#include "device_settings.h"
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif
namespace makxd::detail {
namespace {
void check(bool valid) { if(!valid) throw SettingsError(4); }
uint16_t u16(std::span<const uint8_t> p,size_t at) { return static_cast<uint16_t>(p[at]|(uint16_t(p[at+1])<<8)); }
uint32_t u32(std::span<const uint8_t> p,size_t at) { return p[at]|(uint32_t(p[at+1])<<8)|(uint32_t(p[at+2])<<16)|(uint32_t(p[at+3])<<24); }
void put16(std::span<uint8_t> p,size_t at,uint16_t v) { p[at]=uint8_t(v);p[at+1]=uint8_t(v>>8); }
void put32(std::span<uint8_t> p,size_t at,uint32_t v) { for(unsigned i=0;i<4;i++)p[at+i]=uint8_t(v>>(8*i)); }
std::vector<uint8_t> command(const SettingsQuery& query,uint8_t record,uint8_t op,std::span<const uint8_t> data={},bool pending=false) {
    std::vector<uint8_t> p{record,op};p.insert(p.end(),data.begin(),data.end());auto result=query(p);
    if(result.size()<3 || result[0]!=record || result[1]!=op)throw SettingsError(5);
    if(result[2] && !(pending && result[2]==1))throw SettingsError(result[2]);
    return result;
}
std::array<uint8_t,5> selected(const SettingsInfo& info,uint8_t sections) {
    if(!sections)sections=info.sections;
    if(!sections || (sections&~info.sections))throw SettingsError(5);
    std::array<uint8_t,5> p{};put32(p,0,info.revision);p[4]=sections;return p;
}
std::array<uint8_t,32> hash(std::span<const uint8_t> p) {
    std::array<uint8_t,32> out{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw SettingsError(6);
    auto status=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(p.data()),ULONG(p.size()),out.data(),ULONG(out.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);if(status<0)throw SettingsError(6);
#else
    unsigned count=0;if(EVP_Digest(p.data(),p.size(),out.data(),&count,EVP_sha256(),nullptr)!=1 || count!=32)throw SettingsError(6);
#endif
    return out;
}
void random(std::span<uint8_t> p) {
#ifdef _WIN32
    if(BCryptGenRandom(nullptr,p.data(),ULONG(p.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw SettingsError(6);
#else
    if(RAND_bytes(p.data(),int(p.size()))!=1)throw SettingsError(6);
#endif
}
std::vector<uint8_t> seal(const SettingsQuery& query,std::span<const uint8_t> plain) {
    auto digest=hash(plain);std::vector<uint8_t> file;
    for(size_t offset=0,index=0;offset<plain.size();offset+=96,index++) {
        auto length=std::min(size_t(96),plain.size()-offset);std::vector<uint8_t> p(38+length);
        std::copy(digest.begin(),digest.end(),p.begin());put16(p,32,uint16_t(plain.size()));put16(p,34,uint16_t(index));put16(p,36,uint16_t(length));
        std::copy_n(plain.begin()+offset,length,p.begin()+38);std::vector<uint8_t> reply;
        for(unsigned attempt=0;;attempt++) {
            try {reply=command(query,0x1b,1,p);break;}
            catch(const SettingsError& error) {if(error.status!=2 || attempt==99)throw;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        }
        check(reply.size()==76+length && std::equal(reply.begin()+3,reply.begin()+9,std::array<uint8_t,6>{77,75,83,69,1,1}.begin()) &&
            std::equal(reply.begin()+22,reply.begin()+60,p.begin()));
        file.insert(file.end(),reply.begin()+3,reply.end());
    }
    return file;
}
std::vector<uint8_t> open(const SettingsQuery& query,std::span<const uint8_t> file) {
    check(file.size()>=74 && file.size()<=30000);size_t total=u16(file,51);check(total && total<=16384);
    std::vector<std::span<const uint8_t>> packets;size_t offset=0;
    for(size_t start=0,index=0;start<total;start+=96,index++) {
        auto length=std::min(size_t(96),total-start);check(offset+73+length<=file.size());auto p=file.subspan(offset,73+length);
        check(std::equal(p.begin(),p.begin()+6,std::array<uint8_t,6>{77,75,83,69,1,1}.begin()) &&
            std::equal(p.begin()+19,p.begin()+51,file.begin()+19) && u16(p,51)==total && u16(p,53)==index && u16(p,55)==length);
        packets.push_back(p);offset+=p.size();
    }
    check(offset==file.size());std::vector<uint8_t> plain;
    for(auto p:packets) {auto reply=command(query,0x1b,2,p);check(reply.size()==size_t(3)+u16(p,55));plain.insert(plain.end(),reply.begin()+3,reply.end());}
    auto digest=hash(plain);check(std::equal(digest.begin(),digest.end(),file.begin()+19));return plain;
}
}
std::array<uint8_t,400> settingsEncode(const DeviceSettings& value) {
    std::array<uint8_t,400> p{};const auto& c=value.controller;
    check(c.interpolation<=2 && c.buffer_ms>=1 && c.buffer_ms<=64 && c.timing_variance_percent<=100 && c.curve_enabled<=1 &&
        c.profile_count>=1 && c.profile_count<=4 && c.selected_profile<c.profile_count);
    uint32_t flags=c.curve_enabled|(uint32_t(c.timing_variance_percent)<<22);
    const unsigned shifts[3]={1,8,15};for(unsigned i=0;i<3;i++){check(c.legacy_strengths[i]<=100);flags|=uint32_t(c.legacy_strengths[i])<<shifts[i];}
    put32(p,0,flags);put32(p,4,c.interpolation);put32(p,8,c.buffer_ms);put32(p,12,c.selected_profile);put32(p,16,c.profile_count);
    for(unsigned i=0;i<4;i++) {
        const auto& b=c.behaviors[i];size_t at=20+88*i;
        check(b.enabled<=1);if(!b.enabled){check(i>=c.profile_count);continue;}
        check(b.strength_present<=1 && b.strength_percent<=100 && b.curve_enabled<=1 && b.advanced_enabled<=1 && b.inertia_percent<=100 &&
            b.micro_percent<=100 && b.limit_percent<=100 && b.magnitude_variance_percent<=100 && b.angle_variance_percent<=100);
        size_t length=0;while(length<25 && b.name[length])length++;check(length>=1 && length<=24);
        flags=1|(uint32_t(b.inertia_percent)<<1)|(uint32_t(b.micro_percent)<<8)|(uint32_t(b.limit_percent)<<15)|
            (uint32_t(b.advanced_enabled)<<22)|(uint32_t(b.curve_enabled)<<23);
        put32(p,at,flags);p[at+4]=uint8_t(length);p[at+5]=b.strength_present?uint8_t(128|b.strength_percent):15;
        p[at+6]=b.magnitude_variance_percent;p[at+7]=b.angle_variance_percent;
        for(size_t n=0;n<length;n++){check(b.name[n]>=32 && b.name[n]<=126);p[at+8+n]=uint8_t(b.name[n]);}
        for(unsigned axis=0;axis<4;axis++) {
            const auto& curve=b.curves[axis];size_t pos=at+32+14*axis;
            check(curve.center_deadzone_percent<=50 && curve.anti_deadzone_percent<=50 && curve.change_deadband_percent<=50);
            p[pos]=curve.center_deadzone_percent;p[pos+1]=curve.anti_deadzone_percent;p[pos+2]=curve.change_deadband_percent;p[pos+3]=5;
            for(unsigned point=0;point<5;point++) {
                const auto& v=curve.points[point];check(v.input_percent<=100 && v.output_percent<=100);
                if(!point)check(!v.input_percent && !v.output_percent);
                else check(v.input_percent>curve.points[point-1].input_percent && v.output_percent>=curve.points[point-1].output_percent);
                if(point==4)check(v.input_percent==100 && v.output_percent==100);
                p[pos+4+point*2]=v.input_percent;p[pos+5+point*2]=v.output_percent;
            }
        }
    }
    for(unsigned i=0;i<4;i++) {const auto& m=value.translation[i];check(m.enabled<=1 && m.scale<=(i<2?512:100) && (i>=2 || m.scale) && m.timeout_ms>=1 && m.timeout_ms<=1000);
        put16(p,372+6*i,m.enabled);put16(p,374+6*i,m.scale);put16(p,376+6*i,m.timeout_ms);}
    check(value.mouse_spread_percent<=100);p[396]=value.mouse_spread_percent;return p;
}
DeviceSettings settingsDecode(std::span<const uint8_t> p) {
    check(p.size()==400 && !p[397] && !p[398] && !p[399]);DeviceSettings v{};auto& c=v.controller;
    uint32_t flags=u32(p,0);check(!(flags&~0x1fffffffu) && u32(p,4)<=2 && u32(p,8)<=64 && u32(p,12)<4 && u32(p,16)<=4);
    c.curve_enabled=uint8_t(flags&1);c.interpolation=uint8_t(u32(p,4));c.buffer_ms=uint8_t(u32(p,8));c.selected_profile=uint8_t(u32(p,12));c.profile_count=uint8_t(u32(p,16));
    c.timing_variance_percent=uint8_t((flags>>22)&127);const unsigned shifts[3]={1,8,15};for(unsigned i=0;i<3;i++)c.legacy_strengths[i]=uint8_t((flags>>shifts[i])&127);
    for(unsigned i=0;i<4;i++) {
        auto& b=c.behaviors[i];size_t at=20+88*i;flags=u32(p,at);
        if(!flags){check(std::all_of(p.begin()+at,p.begin()+at+88,[](auto x){return x==0;}));continue;}
        check(!(flags&~0xffffffu) && (flags&1) && p[at+4]>=1 && p[at+4]<=24 && (p[at+5]==15 || (p[at+5]&128)));
        b.enabled=1;b.strength_present=uint8_t(p[at+5]!=15);b.strength_percent=b.strength_present?uint8_t(p[at+5]&127):0;
        b.inertia_percent=uint8_t((flags>>1)&127);b.micro_percent=uint8_t((flags>>8)&127);b.limit_percent=uint8_t((flags>>15)&127);
        b.advanced_enabled=uint8_t((flags>>22)&1);b.curve_enabled=uint8_t((flags>>23)&1);b.magnitude_variance_percent=p[at+6];b.angle_variance_percent=p[at+7];
        for(unsigned n=0;n<p[at+4];n++)b.name[n]=char(p[at+8+n]);
        for(unsigned axis=0;axis<4;axis++) {auto& curve=b.curves[axis];size_t pos=at+32+14*axis;check(p[pos+3]==5);
            curve.center_deadzone_percent=p[pos];curve.anti_deadzone_percent=p[pos+1];curve.change_deadband_percent=p[pos+2];
            for(unsigned n=0;n<5;n++)curve.points[n]={p[pos+4+2*n],p[pos+5+2*n]};}
    }
    for(unsigned i=0;i<4;i++){check(u16(p,372+6*i)<=1);v.translation[i]={uint8_t(u16(p,372+6*i)),u16(p,374+6*i),u16(p,376+6*i)};}
    v.mouse_spread_percent=p[396];(void)settingsEncode(v);return v;
}
SettingsInfo settingsInfo(const SettingsQuery& query) {
    auto p=command(query,0x1d,0);check(p.size()==14 && p[3]==1 && !(p[4]&~7) && !(p[5]&~7) && !p[7] && u16(p,12)==400);
    return {p[4],p[5],p[6],u32(p,8)};
}
namespace {
std::vector<uint8_t> presetKey(std::span<const uint8_t,16> hash) {
    check(std::any_of(hash.begin(),hash.end(),[](uint8_t b){return b!=0;}));
    std::vector<uint8_t> key{2};key.insert(key.end(),hash.begin(),hash.end());return key;
}
void presetComplete(const SettingsQuery& query,const std::vector<uint8_t>& response) {
    check(response.size()==11);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(command(query,0x1e,6,std::span(response).subspan(3,4),true)[2]) {
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Preset operation is still pending");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}
}
ControllerPreset controllerPresetRead(const SettingsQuery& query,std::span<const uint8_t,16> hash) {
    auto key=presetKey(hash);std::array<uint8_t,400> image{};uint32_t revision=0;
    for(unsigned offset=0;offset<396;offset+=96) {
        unsigned length=std::min(96u,396u-offset);auto body=key;
        body.push_back(uint8_t(offset));body.push_back(uint8_t(offset>>8));body.push_back(uint8_t(length));
        auto p=command(query,0x1e,2,body);check(p.size()==9+length && u16(p,7)==offset);
        if(offset && revision!=u32(p,3))throw SettingsError(3);
        revision=u32(p,3);std::copy(p.begin()+9,p.end(),image.begin()+offset);
    }
    auto decoded=settingsDecode(image);ControllerPreset result{};result.controller=decoded.controller;
    std::copy(std::begin(decoded.translation),std::end(decoded.translation),result.translation.begin());return result;
}
void controllerPresetSave(const SettingsQuery& query,std::span<const uint8_t,16> hash,const SettingsSnapshot& snapshot) {
    auto body=presetKey(hash);body.resize(22);put32(body,17,snapshot.info.revision);
    presetComplete(query,command(query,0x1e,3,body,true));
}
SettingsSnapshot controllerPresetLoad(const SettingsQuery& query,std::span<const uint8_t,16> hash) {
    auto body=presetKey(hash);presetComplete(query,command(query,0x1e,4,body,true));return settingsRead(query);
}
SettingsSnapshot settingsRead(const SettingsQuery& query) {
    auto info=settingsInfo(query);std::array<uint8_t,400> image{};
    for(unsigned offset=0;offset<400;offset+=96){auto length=std::min(96u,400-offset);std::array<uint8_t,7> p{};
        put32(p,0,info.revision);put16(p,4,uint16_t(offset));p[6]=uint8_t(length);auto reply=command(query,0x1d,1,p);
        check(reply.size()==9+length && u32(reply,3)==info.revision && u16(reply,7)==offset);std::copy(reply.begin()+9,reply.end(),image.begin()+offset);}
    return {info,settingsDecode(image)};
}
SettingsSnapshot settingsApply(const SettingsQuery& query,const SettingsSnapshot& snapshot,uint8_t sections) {
    auto image=settingsEncode(snapshot.settings);auto begin=command(query,0x1d,2,selected(snapshot.info,sections));check(begin.size()==7);
    std::array<uint8_t,4> token{};std::copy_n(begin.begin()+3,4,token.begin());
    try {
        for(unsigned offset=0;offset<400;offset+=96){auto length=std::min(96u,400-offset);std::vector<uint8_t> p(6+length);std::copy(token.begin(),token.end(),p.begin());
            put16(p,4,uint16_t(offset));std::copy_n(image.begin()+offset,length,p.begin()+6);auto reply=command(query,0x1d,3,p);check(reply.size()==5 && u16(reply,3)==offset+length);}
        (void)command(query,0x1d,4,token);
    } catch(...) {try{(void)command(query,0x1d,6,token);}catch(...){}throw;}
    return settingsRead(query);
}
void settingsSave(const SettingsQuery& query,const SettingsSnapshot& snapshot,uint8_t sections) {
    (void)command(query,0x1d,5,selected(snapshot.info,sections),true);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    for(;;){auto state=settingsInfo(query).save_state;if(!state)return;if(state!=1)throw SettingsError(state);
        if(std::chrono::steady_clock::now()>=deadline)throw SettingsError(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));}
}
std::vector<uint8_t> settingsExport(const SettingsQuery& query,const SettingsSnapshot& snapshot,uint8_t sections) {
    auto current=settingsRead(query);if(current.info.revision!=snapshot.info.revision)throw SettingsError(3);
    auto selection=selected(current.info,sections);auto image=settingsEncode(current.settings);std::array<uint8_t,424> plain{};
    const uint8_t prefix[]={77,75,68,83,1,selection[4],snapshot.info.kinds,0};std::copy_n(prefix,8,plain.begin());random(std::span(plain).subspan(8,16));
    std::copy(image.begin(),image.end(),plain.begin()+24);return seal(query,plain);
}
SettingsSnapshot settingsImport(const SettingsQuery& query,std::span<const uint8_t> file) {
    auto snapshot=settingsRead(query);auto plain=open(query,file);
    check(plain.size()==424 && std::equal(plain.begin(),plain.begin()+5,std::array<uint8_t,5>{77,75,68,83,1}.begin()) && !plain[7] && !(plain[6]&~7) && plain[5]);
    (void)selected(snapshot.info,plain[5]);snapshot.settings=settingsDecode(std::span(plain).subspan(24));return settingsApply(query,snapshot,plain[5]);
}
}
