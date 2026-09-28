#include "AYTest.h"
#include <cstdio>

#ifndef AYUI_TEST_MODULE
#define AYUI_TEST_MODULE "AYUI"
#endif

int main(int argc, char* argv[]) {
    // Preserve failure diagnostics if a native crash interrupts a test.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    return ayt::test::runTests(AYUI_TEST_MODULE, argc, argv);
}
