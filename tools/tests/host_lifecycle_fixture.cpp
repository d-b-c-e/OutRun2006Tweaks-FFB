// Production host lifecycle against synthetic executable memory and fake window
// APIs. Reads the supplied exact game EXE for hashing only; never executes it.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>
#include <wincrypt.h>
#include "hook_mgr.hpp"
#include "plugin.hpp"
#include "game_addrs.hpp"
#include "Proxy.hpp"
#include <cassert>
#include <future>
#include <iostream>
#include <string>
static unsigned char* synthetic = nullptr;
static std::wstring diskPath;
static DWORD fixtureOwner = 0;
static bool pinOk = true, subclassOk = true, windowOk = true, quit = false;
static int pins = 0, subclasses = 0, silences = 0, finalizations = 0, inputFinalizations = 0, discoveryFinalizations = 0, networkFinalizations = 0;
static DWORD captured[9]{}, expectedFlags = 0, beforeEsp = 0, gameCleanups = 0;
static void* hostFunction = nullptr;
static DWORD WINAPI FixtureFileName(HMODULE, LPWSTR out, DWORD capacity) { return static_cast<DWORD>(wcscpy_s(out, capacity, diskPath.c_str())==0 ? diskPath.size() : 0); }
static HMODULE WINAPI FixtureModule(LPCWSTR) { return reinterpret_cast<HMODULE>(synthetic); }
static BOOL WINAPI FixturePin(DWORD flags, LPCWSTR, HMODULE* out) {
    assert(flags==(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN));
    ++pins; *out=reinterpret_cast<HMODULE>(1); return pinOk;
}
static BOOL WINAPI FixtureWindow(HWND hwnd) { return windowOk && hwnd==reinterpret_cast<HWND>(1); }
static DWORD WINAPI FixtureOwner(HWND, DWORD* process) { *process=GetCurrentProcessId(); return fixtureOwner; }
static BOOL WINAPI FixtureSubclass(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR) { ++subclasses; return subclassOk; }
static BOOL WINAPI FixtureRemove(HWND, SUBCLASSPROC, UINT_PTR) { return TRUE; }
static LRESULT WINAPI FixtureDef(HWND, UINT, WPARAM, LPARAM) { return 0; }
static BOOL WINAPI FixturePeek(LPMSG, HWND, UINT, UINT, UINT) { return quit; }
#define GetModuleFileNameW FixtureFileName
#define GetModuleHandleW FixtureModule
#define GetModuleHandleExW FixturePin
#define IsWindow FixtureWindow
#define GetWindowThreadProcessId FixtureOwner
#define SetWindowSubclass FixtureSubclass
#define RemoveWindowSubclass FixtureRemove
#define DefSubclassProc FixtureDef
#define PeekMessageW FixturePeek
#include "../../src/host_lifecycle.cpp"
Hook::Hook() {}
namespace proxy { HMODULE origModule = reinterpret_cast<HMODULE>(2); }
namespace FFB {
void FinalizeSignalCapture() noexcept { assert(finalizations==1 && inputFinalizations==1 && gameCleanups==0 && !ConsumerLifecycle::Runtime().ClaimFinalization()); }
void SilenceForLifecycle() { ++silences; }
void LifecycleIdle() { SilenceForLifecycle(); }
void FinalizeForExit() { assert(gameCleanups==0 && discoveryFinalizations==0); ++finalizations; assert(!ConsumerLifecycle::Runtime().ClaimFinalization()); }
}
// Discovery runs last, after mandatory cleanup and gate completion; even an escaping allocation failure is contained.
namespace TickDiscovery { void FinalizeForExit() { assert(finalizations==1 && inputFinalizations==1 && gameCleanups==0 && !ConsumerLifecycle::Runtime().ClaimFinalization()); ++discoveryFinalizations; throw std::bad_alloc(); } }
namespace DInputRemap { void FinalizeForExit() { assert(finalizations==1 && gameCleanups==0); ++inputFinalizations; } }
void InputManager_FinalizeForExit() { assert(inputFinalizations==1 && gameCleanups==0); }
namespace UPnP { void shutdown() { assert(finalizations==1 && inputFinalizations==1 && gameCleanups==0); ++networkFinalizations; } }

// Deliberately set all general registers/flags before entering the synthetic
// callsite. The displaced cleanup callee records them before doing anything.
__declspec(naked) static void Invoke() {
    __asm {
        pushad
        mov beforeEsp, esp
        mov eax, 111h
        mov ecx, 222h
        mov edx, 333h
        mov ebx, 444h
        mov esi, 555h
        mov edi, 666h
        mov ebp, 777h
        stc
        pushfd
        pop expectedFlags
        call dword ptr [hostFunction]
        popad
        ret
    }
}
static void EmitDword(unsigned char*& p, DWORD value) { memcpy(p,&value,4); p+=4; }
static void MakeCleanup() {
    auto* p=synthetic+0x17E30;
    // mov [absolute], register (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI).
    for (int reg=0;reg<8;++reg) { *p++=0x89; *p++=static_cast<BYTE>(0x05 | reg<<3); EmitDword(p,reinterpret_cast<DWORD>(&captured[reg])); }
    *p++=0x9C; *p++=0x8F; *p++=0x05; EmitDword(p,reinterpret_cast<DWORD>(&captured[8]));
    *p++=0xFF; *p++=0x05; EmitDword(p,reinterpret_cast<DWORD>(&gameCleanups));
    *p++=0xC3;
}
int wmain(int argc, wchar_t** argv) {
    assert(argc==2); diskPath=argv[1]; fixtureOwner=GetCurrentThreadId();
    using namespace OutRunLifecycle; using namespace ConsumerLifecycle;
    synthetic=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x4B0000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)); assert(synthetic);
    const auto base=reinterpret_cast<DWORD>(synthetic);
    for(const auto& signature:CriticalSignatures()) { const auto bytes=RelocatedBytes(signature,base); memcpy(synthetic+signature.rva,bytes.data(),bytes.size()); }
    synthetic[BootstrapRva+10]=0xC3; synthetic[0x17B20]=0xC3; synthetic[0x176F8]=0xC3; MakeCleanup();
    Module::DllHandle=reinterpret_cast<HMODULE>(1);
    assert(!DiskIdentityMatches("wrong") && DiskIdentityMatches(ExactDiskSha256));
    image=synthetic; assert(LoadedIdentityMatches());
    synthetic[CleanupCallRva]^=1; assert(!LoadedIdentityMatches()); synthetic[CleanupCallRva]^=1;
    assert(instance.apply() && hooksInstalled && LoadedIdentityMatches());
    SafetyHookContext context{};
    // Partial installation and failed ownership never enable acquisition.
    hooksInstalled=false; Bootstrap(context); assert(!hostVerified && pins==0); hooksInstalled=true;
    pinOk=false; Bootstrap(context); assert(!hostVerified); pinOk=true;
    diskPath=L"does-not-exist.fixture"; Bootstrap(context); assert(!hostVerified); diskPath=argv[1];
    Bootstrap(context); assert(hostVerified && pins>=3);
    // Startup failure skips the loop and committed cleanup call entirely.
    assert(!ReadyForActuator(reinterpret_cast<void*>(1)));
    hostFunction=synthetic+0x176F8; Invoke(); assert(gameCleanups==0 && finalizations==0);
    LoopBoundary(context);
    windowOk=false; assert(!ReadyForActuator(reinterpret_cast<void*>(1))); windowOk=true;
    auto wrongThread=std::async(std::launch::async,[]{return ReadyForActuator(reinterpret_cast<void*>(1));});
    assert(!wrongThread.get() && subclasses==0);
    subclassOk=false; assert(!ReadyForActuator(reinterpret_cast<void*>(1))); subclassOk=true;
    assert(ReadyForActuator(reinterpret_cast<void*>(1)) && guardedWindow);
    const int beforeSilence=silences;
    Guard(guardedWindow,WM_ACTIVATEAPP,FALSE,0,0,0);
    assert(silences==beforeSilence+2 && Runtime().Current()==Gate::Phase::Running);
    // A conflicting patch after our bootstrap blocks output too.
    synthetic[LoopCallRva]^=1; assert(!ReadyForActuator(reinterpret_cast<void*>(1))); synthetic[LoopCallRva]^=1;
    Guard(guardedWindow,WM_CLOSE,0,0,0,0);
    assert(Runtime().Current()==Gate::Phase::Paused && finalizations==0 && gameCleanups==0);
    ContinuingBoundary(context); assert(Runtime().Current()==Gate::Phase::Running);
    quit=true; Guard(guardedWindow,WM_CLOSE,0,0,0,0); ContinuingBoundary(context);
    assert(Runtime().Current()==Gate::Phase::Paused);
    quit=false; ContinuingBoundary(context); assert(Runtime().Current()==Gate::Phase::Running);
    *reinterpret_cast<DWORD*>(synthetic+0x4A8CAC)=1;
    Guard(guardedWindow,WM_CLOSE,0,0,0,0); ContinuingBoundary(context);
    assert(Runtime().Current()==Gate::Phase::Paused);
    *reinterpret_cast<DWORD*>(synthetic+0x4A8CAC)=0;
    ContinuingBoundary(context); assert(Runtime().Current()==Gate::Phase::Running);
    Guard(guardedWindow,WM_QUERYENDSESSION,0,0,0,0);
    ContinuingBoundary(context); assert(Runtime().Current()==Gate::Phase::Paused);
    Guard(guardedWindow,WM_ENDSESSION,FALSE,0,0,0); assert(Runtime().Current()==Gate::Phase::Running);
    {
        Gate::Lease callback(Runtime());
        Guard(guardedWindow,WM_CLOSE,0,0,0,0); assert(finalizations==0);
        ContinuingBoundary(context); // reentrant resume deferred until outer release
        assert(Runtime().Current()==Gate::Phase::Paused);
    }
    assert(Runtime().Current()==Gate::Phase::Running);
    Guard(guardedWindow,WM_ENDSESSION,TRUE,0,0,0);
    assert(Runtime().Current()==Gate::Phase::Stopping && finalizations==0);
    hostFunction=synthetic+LoopCallRva; Invoke();
    assert(finalizations==1 && inputFinalizations==1 && networkFinalizations==1 && discoveryFinalizations==1 && gameCleanups==1);
    assert(Runtime().Current()==Gate::Phase::Stopped);
    assert(captured[0]==0x111 && captured[1]==0x222 && captured[2]==0x333 && captured[3]==0x444);
    assert(captured[4]==beforeEsp-8 && captured[5]==0x777 && captured[6]==0x555 && captured[7]==0x666);
    assert(captured[8]==expectedFlags);
    CleanupBoundary(context); assert(finalizations==1 && inputFinalizations==1 && networkFinalizations==1 && discoveryFinalizations==1 && gameCleanups==1);
    // Synthetic allocation/hooks intentionally retained until process exit.
    std::cout << "PASS: production exact hash/ASLR signatures/owned-patch conflict check; partial-hook, pin, window, thread and subclass refusal; startup skip; canceled close/session and reentrant recovery; real x86 displaced cleanup CALL preserves registers/flags/stack and runs once after consumer finalization. Synthetic host only.\n";
}
