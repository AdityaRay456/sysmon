    
#include <ncurses.h>
#include <dirent.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <sys/types.h>
#include <utility>

struct ProcInfo {
    int pid;
    std::string name;
    unsigned long utime, stime;
    long rss_kb;
    double cpu_pct;
};

std::pair<unsigned long long, unsigned long long> read_total_and_idle() {
    std::ifstream f("/proc/stat");
    std::string line;
    std::getline(f, line);
    std::istringstream ss(line);
    std::string cpu;
    ss >> cpu;
    unsigned long long user=0, nice=0, system=0, idle=0, iowait=0, irq=0, softirq=0, steal=0;
    ss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long idle_all = idle + iowait;
    return {total, idle_all};
}

unsigned long long read_mem_total_kb() {
    std::ifstream f("/proc/meminfo");
    std::string key;
    unsigned long long val;
    std::string unit;
    while (f >> key >> val >> unit) {
        if (key == "MemTotal:") return val;
    }
    return 0;
}

unsigned long long read_mem_available_kb() {
    std::ifstream f("/proc/meminfo");
    std::string key;
    unsigned long long val;
    std::string unit;
    while (f >> key >> val >> unit) {
        if (key == "MemAvailable:") return val;
    }
    return 0;
}

std::vector<ProcInfo> read_processes() {
    std::vector<ProcInfo> procs;
    DIR *dir = opendir("/proc");
    if (!dir) return procs;

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (!isdigit(entry->d_name[0])) continue;
        int pid = atoi(entry->d_name);
        std::string stat_path = "/proc/" + std::string(entry->d_name) + "/stat";
        std::ifstream stat_file(stat_path);
        if (!stat_file) continue;
        std::string line;
        std::getline(stat_file, line);
        if (line.empty()) continue;

        size_t open = line.find('(');
        size_t close = line.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open) continue;
        std::string name = line.substr(open + 1, close - open - 1);

        std::istringstream ss(line.substr(close + 2));
        std::string tmp;
        for (int i = 0; i < 11 && ss >> tmp; ++i) { /* skip */ }
        unsigned long utime = 0, stime = 0;
        ss >> utime >> stime;

        std::ifstream statm("/proc/" + std::string(entry->d_name) + "/statm");
        long size = 0, resident = 0;
        long rss_kb = 0;
        if (statm) {
            statm >> size >> resident;
            rss_kb = resident * (sysconf(_SC_PAGESIZE) / 1024);
        }

        procs.push_back({pid, name, utime, stime, rss_kb, 0.0});
    }

    closedir(dir);
    return procs;
}

int main() {
    initscr();
    noecho();
    cbreak();
    nodelay(stdscr, TRUE);
    curs_set(0);

    auto prev_total_idle = read_total_and_idle();
    unsigned long long prev_total = prev_total_idle.first;
    unsigned long long prev_idle = prev_total_idle.second;

    auto prev_procs = read_processes();

    const int sleep_ms = 2000; // sample every 2 seconds for stability
    bool sort_by_cpu = true;

    while (true) {
        int ch = getch();
        if (ch == 'q') break;
        if (ch == 's') sort_by_cpu = !sort_by_cpu;

        auto cur_total_idle = read_total_and_idle();
        unsigned long long cur_total = cur_total_idle.first;
        unsigned long long cur_idle = cur_total_idle.second;

        unsigned long long total_delta = (cur_total > prev_total) ? (cur_total - prev_total) : 0;
        unsigned long long idle_delta  = (cur_idle  > prev_idle)  ? (cur_idle  - prev_idle)  : 0;

        double cpu_usage = 0.0;
        if (total_delta > 0) cpu_usage = 100.0 * (double)(total_delta - idle_delta) / (double)total_delta;

        auto curr_procs = read_processes();
        std::unordered_map<int, unsigned long> prev_ticks;
        for (auto &p : prev_procs) prev_ticks[p.pid] = p.utime + p.stime;

        for (auto &p : curr_procs) {
            unsigned long now = p.utime + p.stime;
            unsigned long prev = prev_ticks.count(p.pid) ? prev_ticks[p.pid] : now;
            unsigned long delta = (now > prev) ? (now - prev) : 0;
            p.cpu_pct = (total_delta > 0) ? (100.0 * delta / (double)total_delta) : 0.0;
        }

        unsigned long long total_mem = read_mem_total_kb();
        unsigned long long avail_mem = read_mem_available_kb();
        unsigned long long used_mem = (total_mem > avail_mem) ? (total_mem - avail_mem) : 0;

        std::sort(curr_procs.begin(), curr_procs.end(), [&](const ProcInfo &a, const ProcInfo &b) {
            return sort_by_cpu ? a.cpu_pct > b.cpu_pct : a.rss_kb > b.rss_kb;
        });

        erase();
        mvprintw(0, 0, "SYSTEM MONITOR - q:quit  s:toggle sort (cpu/mem)");
        mvprintw(1, 0, "CPU Usage: %5.2f%% | Mem: %llu/%llu KB | samples(ms): %d",
                 cpu_usage, used_mem, total_mem, sleep_ms);
        mvprintw(2, 0, "%-6s %-22s %8s %10s", "PID", "NAME", "CPU%", "MEM(KB)");

        int row = 3;
        int max_show = std::min((int)curr_procs.size(), 20);
        for (int i = 0; i < max_show; ++i) {
            auto &p = curr_procs[i];
            mvprintw(row + i, 0, "%-6d %-22.22s %8.2f %10ld",
                     p.pid, p.name.c_str(), p.cpu_pct, p.rss_kb);
        }

        refresh();

        prev_total = cur_total;
        prev_idle  = cur_idle;
        prev_procs = curr_procs;

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }

    endwin();
    return 0;
}

