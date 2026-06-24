#include "../include/ui_renderer.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sys/ioctl.h>
#include <unistd.h>

/**
 * @brief Queries terminal configurations to scale structural boundaries
 * responsively.
 */
void UiRenderer::get_terminal_dimensions() {
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
    if (w.ws_col > 0)
      terminal_width = w.ws_col;
    if (w.ws_row > 0)
      terminal_height = w.ws_row;
  }
}

/**
 * @brief Translates an raw byte size speed factor into an auto-scaled string
 * representation.
 */
std::string UiRenderer::format_net_speed(double bytes_per_second) {
  char buf[32];
  if (bytes_per_second >= 1024.0 * 1024.0 * 1024.0) {
    snprintf(buf, sizeof(buf), "%5.1f G/s",
             bytes_per_second / (1024.0 * 1024.0 * 1024.0));
  } else if (bytes_per_second >= 1024.0 * 1024.0) {
    snprintf(buf, sizeof(buf), "%5.1f M/s",
             bytes_per_second / (1024.0 * 1024.0));
  } else if (bytes_per_second >= 1024.0) {
    snprintf(buf, sizeof(buf), "%5.1f K/s", bytes_per_second / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%5.0f   B", bytes_per_second);
  }
  return std::string(buf);
}

/**
 * @brief Generates mathematical 256-color linear ANSI loading bars using
 * percentage scales.
 */
std::string UiRenderer::build_bar_graph(double percentage, int usable_width) {
  int inner_bar_width = usable_width - 2;
  if (inner_bar_width < 4)
    return "[]";

  double clamped_pct = std::clamp(percentage, 0.0, 100.0);
  int filled_width = static_cast<int>((clamped_pct / 100.0) * inner_bar_width);
  std::string out = "[";
  for (int i = 0; i < inner_bar_width; ++i) {
    if (i < filled_width) {
      double ratio = static_cast<double>(i) / inner_bar_width;
      int r = (ratio < 0.5) ? static_cast<int>((ratio * 2.0) * 5.0) : 5;
      int g = (ratio < 0.5)
                  ? 5
                  : static_cast<int>((1.0 - (ratio - 0.5) * 2.0) * 5.0);
      int color_code = 16 + (r * 36) + (g * 6);
      out += "\033[38;5;" + std::to_string(color_code) + "m|";
    } else {
      out += " ";
    }
  }
  out += "\033[0m]";
  return out;
}

void UiRenderer::render(const std::string &static_info,
                        const std::string &load_info,
                        const std::vector<double> &cpu_percentages,
                        const RamMetrics &ram,
                        const std::vector<NetInterfaceData> &networks,
                        const std::vector<DiskMetrics> &disks,
                        const std::vector<ProcessInfo> &processes) {
  get_terminal_dimensions();
  int lines_written = 0;
  std::cout << "\033[2J\033[1;1H";

  std::cout << std::fixed << std::setprecision(1);

  // SECTION 1: SYSTEM INFORMATION
  std::cout << "\033[1;35m[ SYSTEM INFORMATION ]\033[0m\n";
  std::cout << "\033[1;36mSYS:\033[0m" << static_info << "\n";
  std::cout << "\033[1;36mLOAD:\033[0m [ " << load_info << " ]\n";
  std::cout << std::string(terminal_width, '-') << "\n";
  lines_written += 4;

  int half_width = terminal_width / 2;

  // SECTION 2: CPU & MEMORY
  std::cout << "\033[1;33m[ CPU ]\033[0m" << std::string(half_width - 7, ' ')
            << "\033[38;5;242m|\033[0m \033[1;33m[ MEMORY ]\033[0m\n";
  lines_written += 1;

  size_t cpu_mem_rows =
      std::max(cpu_percentages.size(), static_cast<size_t>(1));
  for (size_t r = 0; r < cpu_mem_rows; ++r) {
    if (r < cpu_percentages.size()) {
      std::cout << "\033[1;32mCore " << std::setw(2) << r << " \033[0m";
      std::cout << build_bar_graph(cpu_percentages[r], half_width - 16) << " "
                << std::setw(5) << cpu_percentages[r] << "%";
    } else {
      std::cout << std::string(half_width, ' ');
    }
    std::cout << " \033[38;5;242m|\033[0m ";
    if (r == 0) {
      std::cout << "\033[1;34mRAM   \033[0m";
      std::cout << build_bar_graph(ram.usage_percentage,
                                   (terminal_width - half_width) - 23)
                << " " << ram.used_gb << "G/" << ram.total_gb << "G";
    } else {
      std::cout << std::string((terminal_width - half_width) - 3, ' ');
    }
    std::cout << "\n";
    lines_written += 1;
  }
  std::cout << std::string(terminal_width, '-') << "\n";
  lines_written += 1;

  // SECTION 3: NETWORK & STORAGE
  std::cout << "\033[1;33m[ NETWORK ]\033[0m"
            << std::string(half_width - 11, ' ')
            << "\033[38;5;242m|\033[0m \033[1;33m[ STORAGE ]\033[0m\n";
  lines_written += 1;

  size_t net_disk_rows = std::max(networks.size(), disks.size());
  if (net_disk_rows == 0)
    net_disk_rows = 1;
  for (size_t r = 0; r < net_disk_rows; ++r) {
    if (r < networks.size()) {
      std::string down_str = format_net_speed(networks[r].down_speed_bytes);
      std::string up_str = format_net_speed(networks[r].up_speed_bytes);

      std::cout << std::left << std::setw(5) << networks[r].name
                << " \033[32m▼\033[0m" << down_str << " \033[31m▲\033[0m"
                << up_str;

      int filled_len = networks[r].name.length() + 1 + 2 + down_str.length() +
                       2 + up_str.length() + 1;
      if (half_width > filled_len)
        std::cout << std::string(half_width - filled_len, ' ');
    } else {
      std::cout << std::string(half_width, ' ');
    }
    std::cout << "\033[38;5;242m|\033[0m ";
    if (r < disks.size()) {
      std::cout << "\033[1;34mDISK \033[0m";
      std::cout << build_bar_graph(disks[r].usage_percentage,
                                   (terminal_width - half_width) - 24)
                << " " << disks[r].used_gb << "G/" << disks[r].total_gb << "G";
    } else {
      std::cout << std::string((terminal_width - half_width) - 3, ' ');
    }
    std::cout << "\n";
    lines_written += 1;
  }
  std::cout << std::string(terminal_width, '-') << "\n";
  lines_written += 1;

  // SECTION 4: PROCESS & BINDINGS
  int proc_pane_w = (terminal_width * 75) / 100;
  const int pid_w = 8;
  const int cpu_w = 9;
  const int mem_w = 9;
  const int stat_w = 6;
  int cmd_w = proc_pane_w - (pid_w + cpu_w + mem_w + stat_w);
  if (cmd_w < 10)
    cmd_w = 10;

  std::cout << "\033[1;33m[ PROCESSES ]\033[0m"
            << std::string(std::max(1, proc_pane_w - 13), ' ')
            << " \033[38;5;242m|\033[0m \033[1;33m[ LEGEND & KEYS ]\033[0m\n";

  std::cout
      << "\033[1;35m" << std::left << std::setw(pid_w) << "PID" << std::left
      << std::setw(cmd_w) << "COMMAND" << std::right << std::setw(cpu_w)
      << "CPU%" << std::right << std::setw(mem_w) << "MEM" << std::right
      << std::setw(stat_w) << "STATE"
      << "\033[0m \033[38;5;242m|\033[0m \033[1;35mKEY  FUNCTION\033[0m\n";
  lines_written += 2;

  std::vector<std::string> legend_lines = {
      "q    Exit Tool",     "^C   Exit (Force)", "",
      "STATE   DESCIPTION", "R       Running",   "S       Sleeping",
      "T       Stopped",    "Z       Zombie",    "I       Idle"};

  int remaining_rows = terminal_height - lines_written - 1;
  if (remaining_rows < 1)
    remaining_rows = 5;

  for (int i = 0; i < remaining_rows; ++i) {
    if (i < static_cast<int>(processes.size())) {
      const auto &p = processes[i];
      std::string truncated_cmd = p.name;
      if (truncated_cmd.length() > static_cast<size_t>(cmd_w - 1)) {
        truncated_cmd = truncated_cmd.substr(0, cmd_w - 4) + "...";
      }

      char cpu_buf[32];
      snprintf(cpu_buf, sizeof(cpu_buf), "%.1f%%", p.cpu_pct);
      char mem_buf[32];
      if (p.mem_gb >= 0.1)
        snprintf(mem_buf, sizeof(mem_buf), "%.1fG", p.mem_gb);
      else
        snprintf(mem_buf, sizeof(mem_buf), "%.1fM", p.mem_gb * 1024.0);

      std::cout << std::left << std::setw(pid_w) << p.pid << std::left
                << std::setw(cmd_w) << truncated_cmd << std::right
                << std::setw(cpu_w) << cpu_buf << std::right << std::setw(mem_w)
                << mem_buf << std::right << std::setw(stat_w) << p.state;
    } else {
      std::cout << std::string(proc_pane_w, ' ');
    }

    std::cout << " \033[38;5;242m|\033[0m ";

    if (i < static_cast<int>(legend_lines.size())) {
      std::cout << "\033[38;5;246m" << legend_lines[i] << "\033[0m\n";
    } else {
      std::cout << "\n";
    }
  }
}
