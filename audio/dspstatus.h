#pragma once

#include <string>
#include <vector>

struct DspStatusReport {
    std::vector<std::string> lines;
    int failureCount = 0;

    bool isHealthy() const { return failureCount == 0; }
};

// Prints CurvioEQ DSP architecture summary to stdout.
void printDspArchitectureState();

// Runs all DSP verification checks. Returns number of failed checks (0 = OK).
int runDspVerification();

// Collects architecture + verification lines for in-app log display.
// When includeRingBufferStress is false, uses a quick ring-buffer sanity check instead of the 2 s stress test.
DspStatusReport collectDspStatusReport(bool includeRingBufferStress = true);

// Use WriteConsole on Windows (for GUI subsystem --dsp-status).
void setDspStatusWin32ConsoleOutput(bool enabled);
