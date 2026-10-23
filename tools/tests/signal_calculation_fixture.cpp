// Actual production calculation with observe-only sinks. No native DLL/API calls.
#define OUTRUN_OFFLINE_SIGNALS
#include "../../src/hooks_dinputffb.cpp"
namespace TickDiscovery { void Observe(EVWORK_CAR*, bool) {} void NoteHooks(bool, bool) {} } // inert without an armed window
namespace RemapInject { bool Observe(float, float, int, uint32_t, uint32_t, bool) { return false; } }
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <locale>
float VibrationLeftMotor=0, VibrationRightMotor=0;
double __cdecl sub_1149C0(unsigned int,int,DWORD* water) {
#ifdef OUTRUN_MUTED_FIXTURE
    *water=1;return 0.8;
#else
    std::abort();
#endif
}
Hook::Hook() {}
namespace DInputRemap {
IDirectInputDevice8A* GetPrimaryDevice() { std::abort(); }
bool IsPrimaryInitialized() { std::abort(); }
bool GetPrimaryDeviceGuid(GUID*) { std::abort(); }
int GetTelemetryAccel() { std::abort(); }
int GetTelemetryBrake() { std::abort(); }
UiSnapshot ReadUiSnapshot() { std::abort(); }
}
struct Frame { unsigned sequence, tick; EVWORK_CAR car{}; float roughness; DWORD water; };
static std::vector<Frame> Read(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("input unreadable");
    file.seekg(0,std::ios::end); const auto size=file.tellg();
    if(size<=0 || size>65536) throw std::runtime_error("input file bound");
    file.seekg(-1,std::ios::end);
    if(file.get()!='\n') throw std::runtime_error("final LF required");
    file.seekg(0);
    std::string line; size_t bytes=0;
    auto next=[&]() { if (!std::getline(file,line)) return false;
        bytes+=line.size()+1;
        if(bytes>65536 || line.size()>512 || line.find('\r')!=std::string::npos)
            throw std::runtime_error("input bounds or LF encoding");
        return true; };
    if(!next() || line!="dbce.outrun2006.calculation-input,1,legacy-defaults,60")
        throw std::runtime_error("unsupported schema/version/model/rate");
    std::vector<Frame> frames;
    while(next()) {
        if(line.rfind("complete,",0)==0) {
            if(line!="complete,"+std::to_string(frames.size()) || frames.empty() || next())
                throw std::runtime_error("footer/count/trailing data");
            return frames;
        }
        if(frames.size()>=4096) throw std::runtime_error("frame bound");
        std::vector<std::string> columns; std::istringstream cells(line);
        std::string cell; while(std::getline(cells,cell,',')) columns.push_back(cell);
        if(columns.size()!=12 || line.empty() || line.back()==',')
            throw std::runtime_error("exact numeric columns required");
        double values[12];
        for(int i=0;i<12;i++) {
            // Numeric-only schema: no identities, paths, addresses or free text.
            if(columns[i].empty() || columns[i].find_first_not_of("0123456789.eE+-")!=std::string::npos)
                throw std::runtime_error("non-numeric/private channel");
            std::istringstream number(columns[i]); number.imbue(std::locale::classic());
            if(!(number>>values[i]) || number.peek()!=EOF || !std::isfinite(values[i]))
                throw std::runtime_error("invalid/nonfinite number");
        }
        auto integer=[&](int i,double max) {
            if(values[i]<0 || values[i]>max || std::floor(values[i])!=values[i])
                throw std::runtime_error("integer range");
            return static_cast<unsigned>(values[i]); };
        Frame f{}; f.sequence=integer(0,4095); f.tick=integer(1,4294967295.0);
        if(f.sequence!=frames.size() || (!frames.empty() && f.tick<frames.back().tick))
            throw std::runtime_error("sequence/time order");
        for(int i=2;i<=6;i++) if(std::abs(values[i])>1000) throw std::runtime_error("signal range");
        if(values[2]<0 || values[10]<0 || values[10]>1) throw std::runtime_error("speed/surface range");
        f.car.field_1C4=static_cast<float>(values[2]); f.car.field_1D0=static_cast<float>(values[3]);
        f.car.field_1D4=static_cast<float>(values[4]); f.car.field_264=static_cast<float>(values[5]);
        f.car.field_268=static_cast<float>(values[6]); f.car.cur_gear_208=integer(7,6);
        f.car.field_8=integer(8,65535); f.car.pedal_amount_34=integer(9,255);
        f.roughness=static_cast<float>(values[10]); f.water=integer(11,1); frames.push_back(f);
    }
    throw std::runtime_error("incomplete capture");
}
static DWORD tick=0;
static std::ostringstream observed;
static DWORD WINAPI Clock() { return tick; }
static void Constant(LONG value) {
    value=std::clamp(value,(LONG)-10000,(LONG)10000);
    const LONG scaled=(LONG)std::clamp((float)value*FFB::StrengthScale(),-10000.0f,10000.0f);
    observed<<tick<<",constant,"<<scaled<<"\n"; FFB::prevConstantLevel=value;
}
static void Periodic(int slot,float magnitude,float hz) {
    observed<<tick<<",periodic,"<<slot<<","<<(int)(std::clamp(magnitude,0.0f,1.0f)*FFB::StrengthScale()*10000)
        <<","<<(int)(std::clamp(hz,1.0f,100.0f)*1000)<<"\n";
}
static std::string Run(std::vector<Frame> frames,bool periodic) {
    // No loader, initialization, native sink, telemetry or game lookup is called.
    Settings::FFBDiagnosticLog=false; Settings::TelemetryEnabled=false;
    FFB::ffbLoaded=true; // calculation branch availability, not an actual module
    FFB::useSharedModel=false; FFB::periodicsActive=periodic;
    FFB::slotRoadTexture=0; FFB::slotTireSlip=1;
    FFB::ResetCalculationState(); observed.str(""); observed.clear(); observed.imbue(std::locale::classic());
    observed<<"dbce.outrun2006.calculation-observation,1,observe,physicalOutput=false,legacy-defaults,60\n";
    for(auto& f:frames) {
        tick=f.tick; observed<<"frame,"<<f.sequence<<","<<tick<<"\n";
        FFB::CalculateSignals(&f.car,f.roughness,f.water,{Constant,Periodic},Clock);
    }
    observed<<"complete,"<<frames.size()<<"\n"; return observed.str();
}
static void SharedEvents() {
    auto require=[](bool ok,const char* why) { if(!ok)throw std::runtime_error(why); };
    dbce::force::ModelSettings settings;
    settings.impact_strength=0.4f; settings.shift_strength=0.2f;
    dbce::force::Model model(settings);
    dbce::force::Shaper shaper{dbce::force::ShaperSettings()};
    FFB::sharedModel=&model; FFB::sharedShaper=&shaper;
    FFB::ffbLoaded=true; FFB::useSharedModel=true; FFB::periodicsActive=false;
    Settings::FFBDiagnosticLog=false; Settings::TelemetryEnabled=false;
    EVWORK_CAR car{};
    auto step=[&](float speed,uint32_t flags,uint32_t gear) {
        car.field_1C4=speed; car.field_8=flags; car.cur_gear_208=gear;
        FFB::CalculateSignals(&car,0,0,{Constant,Periodic},Clock);
        return model.last_was_event;
    };
    FFB::ResetCalculationState();
    require(step(.8f,0x1000,2),"first contact impact missing");
    for(int i=0;i<20;++i)step(.8f,0x1000,2);
    require(!model.last_was_event,"held contact restarts the impact envelope");
    step(.8f,0,2);
    require(!step(.8f,0x1000,2),"contact edge bypassed crash cooldown");
    for(int i=0;i<100;++i)
        require(!step(.8f,0x1000,2),"held contact retriggered after cooldown");
    require(FFB::crashImpulseTimer==0,"shared crash cooldown never expired");
    step(.8f,0,2);
    require(step(.8f,0x1000,2),"second contact impact missing");

    FFB::ResetCalculationState();
    require(!model.last_was_event && FFB::crashImpulseTimer==0,"reset retained impact state");
    for(int i=0;i<8;++i)step(.8f,0,2);
    require(step(.7f,0,2),"first speed-window impact missing");
    for(int i=0;i<100;++i) {
        const bool event=step(.7f,0,2);
        if(i>=20)require(!event,"speed-window impact envelope never expired");
    }
    for(int i=0;i<8;++i)step(.8f,0,2);
    require(step(.6f,0,2),"second speed-window impact missing");

    FFB::ResetCalculationState();
    require(!step(.8f,0,3),"initial gear emitted a shift");
    require(step(.8f,0,4),"gear edge emitted no shift");
    for(int i=0;i<20;++i)step(.8f,0,4);
    require(!model.last_was_event,"held gear restarted the shift envelope");
    require(step(.8f,0,5),"second gear edge emitted no shift");
    FFB::ResetCalculationState();
    require(!step(.8f,0,6),"reset retained gear edge or event envelope");
    FFB::sharedModel=nullptr; FFB::sharedShaper=nullptr; FFB::useSharedModel=false;
    std::cout<<"PASS: shared contact/speed impacts expire and retrigger; held contact/cooldown suppression; gear edges, expiry and reset; memory-only sinks\n";
}
int main(int argc,char** argv) {
    try {
        spdlog::set_level(spdlog::level::off); // Keep wall-clock diagnostics out of observations.
        if(argc==2 && std::string_view(argv[1])=="--shared-events") { SharedEvents(); return 0; }
        if(argc!=2 && argc!=3) throw std::runtime_error("usage: signal-calculation input [expected-observation]");
        auto frames=Read(argv[1]); auto result=Run(frames,false);
        if(result!=Run(frames,false) || Run(frames,true)!=Run(frames,true))
            throw std::runtime_error("reset determinism failure");
        if(result!=Run(frames,false))
            throw std::runtime_error("periodic-to-constant reset failure");
        if(argc==3) {
            std::ifstream file(argv[2],std::ios::binary); if(!file) throw std::runtime_error("expected unreadable");
            file.seekg(0,std::ios::end); const auto size=file.tellg();
            if(size<0 || size>1048576) throw std::runtime_error("expected bound");
            file.seekg(0);
            std::string expected((std::istreambuf_iterator<char>(file)),{});
            if(expected.size()>1048576) throw std::runtime_error("expected bound");
            if(expected!=result) { std::cerr<<"observation mismatch\n"; return 1; }
        }
        std::cout<<result; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 2; }
}
