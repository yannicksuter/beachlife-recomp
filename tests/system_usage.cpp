#include "platform/system_usage.h"
#include "platform/system_usage_detail.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace beachlife::platform;
using namespace beachlife::platform::detail;
unsigned checks;
void check(bool value, const char* message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try {
        UsageDelta cpu;
        check(!cpu.sample(CpuTimes{100, 1000}), "First sample is unavailable");
        check(cpu.sample(CpuTimes{300, 1400}) == 50.0, "All CPUs share the total-time denominator");
        check(cpu.sample(CpuTimes{300, 1600}) == 0.0, "Idle is a measured zero");
        check(cpu.sample(CpuTimes{500, 1800}) == 100.0, "Fully busy");
        check(!cpu.sample(CpuTimes{500, 1800}), "No elapsed time is unavailable");
        check(!cpu.sample(CpuTimes{0, 0}), "Counter reset does not underflow");
        check(cpu.sample(CpuTimes{20, 100}) == 20.0, "Recovery after counter reset");
        check(!cpu.sample(std::nullopt) && !cpu.sample(CpuTimes{20, 100}), "Missing reading resets baseline");
        check(!cpu.sample(CpuTimes{220, 200}), "Inconsistent counters do not fabricate a load");

        std::istringstream stat("cpu 100 20 30 400 50 10 5 15 70 10\ncpu0 1 2 3 4 5 6 7 8\n");
        const auto times = read_proc_cpu_times(stat);
        check(times && times->busy == 180 && times->total == 630, "I/O wait idle; guest time not double counted");
        std::istringstream truncated("cpu 10 20"), perCpu("cpu0 1 2 3 4 5 6 7 8");
        check(!read_proc_cpu_times(truncated) && !read_proc_cpu_times(perCpu), "Incomplete/non-total input rejected");
        check(!usage_percent(-1) && !usage_percent(std::numeric_limits<double>::quiet_NaN())
            && !usage_percent(std::numeric_limits<double>::infinity()), "Invalid driver readings stay unavailable");
        check(usage_percent(110) == 100 && usage_percent(0) == 0, "Bounded percentages preserve zero");

        GpuEngineUsage gpu;
        check(!gpu.busiest(), "Missing GPU counters are N/A, not zero");
        gpu.add(L"pid_10_luid_0x0_0x123_phys_0_eng_0_engtype_3D", 25);
        gpu.add(L"pid_20_luid_0x0_0x123_phys_0_eng_0_engtype_3D", 45);
        gpu.add(L"pid_10_luid_0x0_0x123_phys_0_eng_1_engtype_Copy", 60);
        check(gpu.busiest() == 70, "External processes add on one engine; independent engines do not add");
        gpu.add(L"pid_30_luid_0x0_0x456_phys_0_eng_0_engtype_3D", 80);
        check(gpu.busiest() == 80, "Different GPUs do not share an engine");
        gpu.add(L"unexpected", 100);
        gpu.add(L"pid_40_luid_0x0_0x456_phys_0_eng_0_engtype_3D", -1);
        check(gpu.busiest() == 80, "Malformed and invalid instances ignored");
        gpu.add(L"pid_40_luid_0x0_0x456_phys_0_eng_0_engtype_3D", 30);
        check(gpu.busiest() == 100, "Small sampling overlap stays within 100 percent");

        if (argc > 1 && std::string(argv[1]) == "--sample") {
            SystemUsageMonitor monitor;
            for (int n = 0; n < 4; ++n) {
                std::this_thread::sleep_for(std::chrono::milliseconds(550));
                const auto sample = monitor.latest();
                const auto format = [](std::optional<double> value) { return value ? std::to_string(*value) : "N/A"; };
                std::cout << "CPU " << format(sample.cpu_percent) << " GPU " << format(sample.gpu_percent) << '\n';
                check(!sample.cpu_percent || (*sample.cpu_percent >= 0 && *sample.cpu_percent <= 100), "Live CPU range");
                check(!sample.gpu_percent || (*sample.gpu_percent >= 0 && *sample.gpu_percent <= 100), "Live GPU range");
            }
        }
        std::cout << checks << " system usage checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
