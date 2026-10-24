#define UPNP_GetSpecificPortMappingEntry FixtureGet
#define UPNP_AddPortMapping FixtureAdd
#define UPNP_DeletePortMapping FixtureDelete
#include "../../src/upnp.cpp"
#undef UPNP_GetSpecificPortMappingEntry
#undef UPNP_AddPortMapping
#undef UPNP_DeletePortMapping
#include <cassert>

static int lookupResult = 714, addResult = 0, additions = 0, deletions = 0;
static const char* existingClient = "192.168.1.5";
static const char* existingPort = "41455";
static const char* existingDescription = "OutRun2006";

extern "C" int FixtureGet(const char*, const char*, const char*, const char*, const char*,
    char* client, char* port, char* description, char* enabled, char* duration) {
    if (lookupResult == 0) {
        std::strcpy(client, existingClient);
        std::strcpy(port, existingPort);
        std::strcpy(description, existingDescription);
        std::strcpy(enabled, "1");
        std::strcpy(duration, "0");
    }
    return lookupResult;
}
extern "C" int FixtureAdd(const char*, const char*, const char*, const char*, const char*,
    const char*, const char*, const char*, const char*) { ++additions; return addResult; }
extern "C" int FixtureDelete(const char*, const char*, const char*, const char*, const char*) {
    ++deletions; return 0;
}

int main() {
    SetEnvironmentVariableW(L"DBCE_OUTRUN_SIGNAL_MUTE", L"legacy");
    UPnP::refresh();
    assert(!UPnP::state().worker && !UPnP::busy());
    UPNPUrls urls{};
    urls.controlURL = const_cast<char*>("fixture");
    IGDdatas data{};
    bool added = false, owned = false;
    int error = -1;
    lookupResult = 0;
    UPnP::add_if_absent(urls, data, "41455", "TCP", "192.168.1.5", added, error, owned);
    assert(added && !owned && additions == 0); // Preexisting owner mapping stays unclaimed.
    existingClient = "192.168.1.9";
    UPnP::add_if_absent(urls, data, "41455", "TCP", "192.168.1.5", added, error, owned);
    assert(!added && !owned && error == 718 && additions == 0);
    lookupResult = -3;
    UPnP::add_if_absent(urls, data, "41455", "TCP", "192.168.1.5", added, error, owned);
    assert(!added && !owned && additions == 0); // A router read error must not create a rule.
    lookupResult = 714;
    UPnP::add_if_absent(urls, data, "41455", "TCP", "192.168.1.5", added, error, owned);
    assert(added && owned && additions == 1);

    auto& state = UPnP::state();
    state.mapped = true;
    state.controlUrl = "fixture";
    state.serviceType = "fixture";
    state.lanAddress = "192.168.1.5";
    state.owned[0][0] = true;
    lookupResult = 0;
    existingClient = "192.168.1.9";
    UPnP::shutdown();
    assert(deletions == 0); // A reassigned rule belongs to somebody else.
    state.mapped = true;
    state.owned[0][0] = true;
    existingClient = "192.168.1.5";
    UPnP::shutdown();
    assert(deletions == 1 && !state.owned[0][0]);
}
