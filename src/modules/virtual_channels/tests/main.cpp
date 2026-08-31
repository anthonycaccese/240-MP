#include "TestHarness.h"

#include <QCoreApplication>

int runChannelScheduleTests();
int runPathGuardTests();
int runChannelTunerTests();
int runScheduleGeneratorTests();
int runDurationProbeTests();
int runMediaServerSourceTests();
int runLocalLibraryTests();
int runVirtualChannelsBackendTests();

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    runChannelScheduleTests();
    runPathGuardTests();
    runChannelTunerTests();
    runScheduleGeneratorTests();
    runDurationProbeTests();
    runMediaServerSourceTests();
    runLocalLibraryTests();
    runVirtualChannelsBackendTests();

    std::printf("\n%d checks, %d failed\n", vtest::g_checks, vtest::g_failed);
    return vtest::g_failed == 0 ? 0 : 1;
}
