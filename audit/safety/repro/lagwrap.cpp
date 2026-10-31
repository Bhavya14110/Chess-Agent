// lagwrap: sits between a UCI GUI (fastchess) and an engine and delays every engine->GUI line
// by a random LAGWRAP_MIN..LAGWRAP_MAX ms (default 100..300), keeping line order. GUI->engine
// lines pass through at once. Simulates Lichess network lag on the move-sending path.
//
// env: LAGWRAP_ENGINE  engine exe (required unless given as argv[1])
//      LAGWRAP_MIN / LAGWRAP_MAX  delay range in ms
//      LAGWRAP_LOG      optional file: one line per bestmove "recv_ms delay_ms line"
// build: g++ -O2 -std=c++20 -static lagwrap.cpp -o lagwrap.exe
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <thread>

using Clock = std::chrono::steady_clock;

struct Item { std::string line; Clock::time_point release; bool eof; };

static std::mutex mu;
static std::condition_variable cv;
static std::deque<Item> q;

static int envint(const char* k, int def) {
    const char* v = std::getenv(k);
    return v && *v ? std::atoi(v) : def;
}

int main(int argc, char** argv) {
    std::string engine = argc > 1 ? argv[1] : (std::getenv("LAGWRAP_ENGINE") ? std::getenv("LAGWRAP_ENGINE") : "");
    if (engine.empty()) { std::fprintf(stderr, "lagwrap: set LAGWRAP_ENGINE\n"); return 2; }
    int lo = envint("LAGWRAP_MIN", 100), hi = envint("LAGWRAP_MAX", 300);
    FILE* log = nullptr;
    if (const char* lp = std::getenv("LAGWRAP_LOG"); lp && *lp) log = std::fopen(lp, "a");

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inR, inW, outR, outW;
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) return 3;
    SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inR;
    si.hStdOutput = outW;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + engine + "\"";
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        std::fprintf(stderr, "lagwrap: cannot start %s (%lu)\n", engine.c_str(), GetLastError());
        return 4;
    }
    CloseHandle(inR);
    CloseHandle(outW);

    // GUI -> engine, no delay
    std::thread fwd([&] {
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        char buf[4096];
        DWORD n, w;
        while (ReadFile(in, buf, sizeof(buf), &n, nullptr) && n > 0)
            if (!WriteFile(inW, buf, n, &w, nullptr)) break;
        CloseHandle(inW);
    });
    fwd.detach();

    // engine -> queue, each line stamped with its release time
    std::thread rd([&] {
        // random_device alone gave every process the same seed (MinGW), so mix in pid and clock
        std::seed_seq seed{std::random_device{}(), (unsigned)GetCurrentProcessId(),
                           (unsigned)Clock::now().time_since_epoch().count()};
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(lo, hi);
        Clock::time_point last = Clock::now();
        std::string acc;
        char buf[4096];
        DWORD n;
        auto t0 = Clock::now();
        while (ReadFile(outR, buf, sizeof(buf), &n, nullptr) && n > 0) {
            acc.append(buf, n);
            size_t p;
            while ((p = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, p + 1);
                acc.erase(0, p + 1);
                auto now = Clock::now();
                int d = dist(rng);
                auto rel = std::max(now + std::chrono::milliseconds(d), last);
                last = rel;
                if (log && line.rfind("bestmove", 0) == 0) {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
                    auto eff = std::chrono::duration_cast<std::chrono::milliseconds>(rel - now).count();
                    std::fprintf(log, "%lld %lld %s", (long long)ms, (long long)eff, line.c_str());
                    std::fflush(log);
                }
                std::lock_guard<std::mutex> lk(mu);
                q.push_back({line, rel, false});
                cv.notify_one();
            }
        }
        std::lock_guard<std::mutex> lk(mu);
        q.push_back({acc, last, true});
        cv.notify_one();
    });

    // queue -> GUI, after the delay
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    for (;;) {
        Item it;
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [] { return !q.empty(); });
            it = q.front();
            q.pop_front();
        }
        std::this_thread::sleep_until(it.release);
        DWORD w;
        if (!it.line.empty()) WriteFile(out, it.line.data(), (DWORD)it.line.size(), &w, nullptr);
        if (it.eof) break;
    }
    rd.join();
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (log) std::fclose(log);
    return (int)code;
}
