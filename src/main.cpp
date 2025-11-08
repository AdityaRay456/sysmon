// src/main.cpp
#include <ncurses.h>
#include <dirent.h>
#include <iomanip>
#include <cstring>   
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
#include <ctime>
#include <iomanip>

struct ProcInfo {
    int pid;
    std::string name;
    unsigned long utime, stime;
    long rss_kb;
    double cpu_pct;
};

static const int SAMPLE_MS = 2000;
static const int MAX_ROWS = 20;

// ---------- proc reading helpers ----------
std::pair<unsigned long long, unsigned long long> read_total_and_idle() {
    std::ifstream f("/proc/stat");
    std::string line;
    if (!std::getline(f, line)) return {0,0};
    std::istringstream ss(line);
    std::string cpu;
    ss >> cpu;
    unsigned long long user=0,nice=0,system=0,idle=0,iowait=0,irq=0,softirq=0,steal=0;
    ss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long idle_all = idle + iowait;
    return {total, idle_all};
}

std::pair<unsigned long long, unsigned long long> read_mem_kb() {
    std::ifstream f("/proc/meminfo");
    std::string key; unsigned long long val; std::string unit;
    unsigned long long total=0, avail=0;
    while (f >> key >> val >> unit) {
        if (key == "MemTotal:") total = val;
        if (key == "MemAvailable:") { avail = val; break; }
    }
    return {total, avail};
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

unsigned long long read_uptime_seconds() {
    std::ifstream f("/proc/uptime");
    double up=0.0;
    if (f >> up) return (unsigned long long)up;
    return 0;
}

// ---------- ncurses UI helpers ----------
void init_colors_safe() {
    if (!has_colors()) return;
    start_color();
    use_default_colors();
    init_pair(1, COLOR_GREEN, -1);   // low usage
    init_pair(2, COLOR_YELLOW, -1);  // medium
    init_pair(3, COLOR_RED, -1);     // high
    init_pair(4, COLOR_CYAN, -1);    // headings
    init_pair(5, COLOR_MAGENTA, -1); // footer
    init_pair(6, COLOR_WHITE, COLOR_BLUE); // title bar (if supported)
}

void draw_boxed_header(int width, double cpu_usage, unsigned long long used_mem, unsigned long long total_mem) {
    // Title centered
    std::string title = " SYS MONITOR ";
    int title_pos = (width - (int)title.size())/2;
    attron(A_BOLD);
    mvhline(0, 0, ' ', width);
    mvprintw(0, title_pos, "%s", title.c_str());
    attroff(A_BOLD);

    // CPU and memory at right
    char buf[128];
    snprintf(buf, sizeof(buf), "CPU: %5.2f%%  MEM: %llu/%llu KB", cpu_usage, used_mem, total_mem);
    int right_pos = width - (int)strlen(buf) - 2;
    attron(COLOR_PAIR(4));
    mvprintw(0, std::max(0, right_pos), "%s", buf);
    attroff(COLOR_PAIR(4));
}

void draw_column_headers(int start_row) {
    attron(A_UNDERLINE | A_BOLD);
    mvprintw(start_row, 1, "%-6s %-22s %8s %10s", "PID", "NAME", "CPU%", "MEM(KB)");
    attroff(A_UNDERLINE | A_BOLD);
}

void draw_footer(int height, int width, unsigned long long uptime_s, int proc_count, int sample_ms) {
    int row = height - 1;
    std::string uptime;
    unsigned long long days = uptime_s / 86400;
    unsigned long long hours = (uptime_s % 86400) / 3600;
    unsigned long long mins = (uptime_s % 3600) / 60;
    unsigned long long secs = uptime_s % 60;
    std::ostringstream oss;
    if (days) oss << days << "d ";
    oss << hours << "h " << mins << "m";
    uptime = oss.str();

    attron(COLOR_PAIR(5));
    mvhline(row, 0, ' ', width);
    mvprintw(row, 1, "Uptime: %s   Procs: %d   Sample: %dms", uptime.c_str(), proc_count, sample_ms);
    attroff(COLOR_PAIR(5));
}

void print_proc_row(int row, const ProcInfo &p, bool highlight) {
    if (highlight) attron(A_REVERSE);
    // Choose color by CPU%
    if (p.cpu_pct >= 50.0) attron(COLOR_PAIR(3));
    else if (p.cpu_pct >= 20.0) attron(COLOR_PAIR(2));
    else attron(COLOR_PAIR(1));
    mvprintw(row, 1, "%-6d %-22.22s %8.2f %10ld", p.pid, p.name.c_str(), p.cpu_pct, p.rss_kb);
    // turn color off
    attroff(COLOR_PAIR(1)); attroff(COLOR_PAIR(2)); attroff(COLOR_PAIR(3));
    if (highlight) attroff(A_REVERSE);
}

// ---------- main ----------
int main() {
    initscr();
    noecho();
    cbreak();
    curs_set(0);
    nodelay(stdscr, TRUE); // non-blocking getch
    keypad(stdscr, TRUE);

    init_colors_safe();

    auto prev_total_idle = read_total_and_idle();
    unsigned long long prev_total = prev_total_idle.first;
    unsigned long long prev_idle = prev_total_idle.second;
    auto prev_procs = read_processes();

    bool sort_by_cpu = true;
    while (true) {
        int ch = getch();
        if (ch == 'q') break;
        if (ch == 's') sort_by_cpu = !sort_by_cpu;

        // sample
        auto cur_total_idle = read_total_and_idle();
        unsigned long long cur_total = cur_total_idle.first;
        unsigned long long cur_idle = cur_total_idle.second;

        unsigned long long total_delta = (cur_total > prev_total) ? (cur_total - prev_total) : 1;
        unsigned long long idle_delta  = (cur_idle  > prev_idle)  ? (cur_idle  - prev_idle)  : 0;
        double cpu_usage = total_delta > 0 ? 100.0 * (double)(total_delta - idle_delta) / (double)total_delta : 0.0;

        auto curr_procs = read_processes();
        std::unordered_map<int, unsigned long> prev_ticks;
        for (auto &p : prev_procs) prev_ticks[p.pid] = p.utime + p.stime;

        for (auto &p : curr_procs) {
            unsigned long now = p.utime + p.stime;
            unsigned long prev = prev_ticks.count(p.pid) ? prev_ticks[p.pid] : now;
            unsigned long delta = (now > prev) ? (now - prev) : 0;
            p.cpu_pct = (total_delta > 0) ? (100.0 * delta / (double)total_delta) : 0.0;
        }

        auto mem = read_mem_kb();
        unsigned long long total_mem = mem.first;
        unsigned long long avail_mem = mem.second;
        unsigned long long used_mem = (total_mem > avail_mem) ? (total_mem - avail_mem) : 0;

        // sort
        std::sort(curr_procs.begin(), curr_procs.end(), [&](const ProcInfo &a, const ProcInfo &b) {
            return sort_by_cpu ? a.cpu_pct > b.cpu_pct : a.rss_kb > b.rss_kb;
        });

        // layout
        int height, width;
        getmaxyx(stdscr, height, width);
        erase();

        // header
        draw_boxed_header(width, cpu_usage, used_mem, total_mem);

        // column header (row 2)
        draw_column_headers(2);

        // process rows
        int start_row = 3;
        int to_show = std::min((int)curr_procs.size(), MAX_ROWS);
        for (int i = 0; i < to_show; ++i) {
            bool highlight = false;
            print_proc_row(start_row + i, curr_procs[i], highlight);
        }

        // footer
        unsigned long long uptime_s = read_uptime_seconds();
        int proc_count = (int)curr_procs.size();
        draw_footer(height, width, uptime_s, proc_count, SAMPLE_MS);

        refresh();

        // prepare next sample
        prev_total = cur_total;
        prev_idle  = cur_idle;
        prev_procs = curr_procs;

        // sleep while still responsive to keys
        int slept = 0;
        int interval = SAMPLE_MS;
        while (slept < interval) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            slept += 100;
            int c = getch();
            if (c == 'q') { endwin(); return 0; }
            if (c == 's') { sort_by_cpu = !sort_by_cpu; break; }
        }
    }

    endwin();
    return 0;
}


