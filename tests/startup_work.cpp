// The work behind the game window's loading screen: order, progress, cancellation and errors.
#include "runtime/startup.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace beachlife::runtime;
using namespace std::chrono_literals;

void expect(bool value, const std::source_location& where = std::source_location::current()) {
    if (!value) throw std::runtime_error("Startup work check failed at line " + std::to_string(where.line()));
}
void wait_for(const std::function<bool()>& condition) {
    const auto until = std::chrono::steady_clock::now()+15s;
    while (!condition()) {
        expect(std::chrono::steady_clock::now() < until);
        std::this_thread::sleep_for(1ms);
    }
}
// Blocks a step until released, so checks do not depend on scheduling.
struct Gate {
    std::atomic_bool entered{false}, released{false};
    void pass(const StartupProgress& progress) {
        entered = true;
        wait_for([&] { return released || progress.cancelled(); });
    }
};

int main() {
    try {
        {
            StartupWork none({});
            wait_for([&] { return none.status().ready; });
            expect(none.status().share() == 1.0f && !none.status().failed);
        }
        {
            Gate unknown, later_unknown, known;
            std::string order;
            StartupWork work({
                {"Loading game files", [&](StartupProgress& progress) { order += 'a'; unknown.pass(progress); }},
                {"Preparing graphics", [&](StartupProgress& progress) {
                    order += 'b';
                    later_unknown.pass(progress);
                    progress.total(4);
                    progress.advance(1);
                    known.pass(progress);
                    progress.advance(3);
                }},
            });
            wait_for([&] { return unknown.entered.load(); });
            auto status = work.status();
            expect(status.label == "Loading game files" && status.step == 0 && status.steps == 2);
            expect(!status.share()); // the bar sweeps until a size is known
            unknown.released = true;
            wait_for([&] { return later_unknown.entered.load(); });
            expect(work.status().share() == 0.5f); // after the start the bar holds instead of sweeping again
            later_unknown.released = true;
            wait_for([&] { return known.entered.load(); });
            status = work.status();
            expect(status.label == "Preparing graphics" && status.step == 1 && status.done == 1 && status.total == 4);
            expect(status.share() == 0.625f && !status.ready);
            known.released = true;
            wait_for([&] { return work.status().ready; });
            expect(order == "ab" && work.status().step == 2 && work.status().share() == 1.0f);
        }
        {
            // Leaving stops the running step and skips the rest; it is not an error.
            Gate gate;
            std::atomic_bool returned{false}, later{false};
            auto work = std::make_unique<StartupWork>(std::vector<StartupStep>{
                {"first", [&](StartupProgress& progress) { gate.pass(progress); returned = true; }},
                {"second", [&](StartupProgress&) { later = true; }}});
            wait_for([&] { return gate.entered.load(); });
            work->cancel();
            wait_for([&] { return returned.load(); });
            work.reset();
            expect(!later);
        }
        {
            Gate gate;
            std::atomic_bool later{false};
            auto work = std::make_unique<StartupWork>(std::vector<StartupStep>{
                {"first", [&](StartupProgress& progress) { gate.pass(progress); }},
                {"second", [&](StartupProgress&) { later = true; }}});
            wait_for([&] { return gate.entered.load(); });
            work.reset(); // closing the window mid-step cancels and waits for it
            expect(!later);
        }
        {
            std::atomic_bool later{false};
            StartupWork work({{"broken", [](StartupProgress&) { throw std::runtime_error("Cannot read BeachLife.ini"); }},
                              {"second", [&](StartupProgress&) { later = true; }}});
            wait_for([&] { return work.status().failed; });
            expect(!work.status().ready && !later);
            try {
                work.rethrow();
                expect(false);
            } catch (const std::runtime_error& error) {
                expect(std::string(error.what()) == "Cannot read BeachLife.ini");
            }
        }
        std::cout << "Startup order, progress shares, cancellation and errors passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
