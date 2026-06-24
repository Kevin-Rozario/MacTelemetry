/**
 * @file main.cpp
 * @brief Multi-threaded performance monitoring engine orchestration loop for
 * macOS.
 * @author Kevin Rozario
 * @license MIT
 */

#include "../include/sys_info.hpp"
#include "../include/ui_renderer.hpp"
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <mutex>
#include <termios.h>
#include <thread>
#include <unistd.h>

// Global runtime constraints
std::atomic<bool> g_is_running{true};
std::mutex g_shutdown_mutex;
std::condition_variable g_shutdown_cv;

/**
 * @class TerminalManager
 * @brief RAII controller managing raw terminal mode modifications and graceful
 * graphic resets.
 */
class TerminalManager {
private:
  struct termios m_original_state;
  bool m_is_raw_active{false};

public:
  TerminalManager() {
    // Hide standard blinking text cursor block immediately
    std::cout << "\033[?25l" << std::flush;
  }

  ~TerminalManager() { disable_raw_mode(); }

  /**
   * @brief Modifies STDIN flags to capture keystrokes instantaneously without
   * character echoes.
   */
  void enable_raw_mode() {
    if (m_is_raw_active)
      return;

    if (tcgetattr(STDIN_FILENO, &m_original_state) == 0) {
      struct termios raw = m_original_state;
      raw.c_lflag &= ~(ECHO | ICANON);
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;

      if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
        m_is_raw_active = true;
      }
    }
  }

  /**
   * @brief Restores canonical TTY configurations and safely unrolls the visible
   * screen state.
   */
  void disable_raw_mode() {
    if (m_is_raw_active) {
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &m_original_state);
      m_is_raw_active = false;
    }
    // Restore text cursor visibility state safely
    std::cout << "\033[?25h" << std::flush;
  }
};

/**
 * @brief Dispatches low-level kernel signals onto thread-safe global flags.
 * @param signal_code Incoming POSIX kernel signal code.
 */
void handle_system_signals(int signal_code) {
  (void)signal_code;
  g_is_running = false;
  g_shutdown_cv.notify_all();
}

/**
 * @brief Asynchronous thread loop processing input keystrokes from standard
 * device pipes.
 * @param term_mgr Reference to the terminal manager engine.
 */
void worker_keyboard_listener(TerminalManager &term_mgr) {
  term_mgr.enable_raw_mode();

  while (g_is_running) {
    char input_char;
    if (read(STDIN_FILENO, &input_char, 1) > 0) {
      if (input_char == 'q' || input_char == 'Q') {
        g_is_running = false;
        g_shutdown_cv.notify_all();
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }

  term_mgr.disable_raw_mode();
}

int main() {
  // Untie C++ streams from C legacy libraries to minimize UI sync bottlenecks
  std::ios_base::sync_with_stdio(false);
  std::cin.tie(nullptr);

  // Register clean operational trap vectors
  std::signal(SIGINT, handle_system_signals);
  std::signal(SIGTERM, handle_system_signals);

  // Instantiate RAII scopes
  TerminalManager terminal_controller;
  SysInfo metrics_provider;
  UiRenderer frame_renderer;

  // Initialize data collection pools
  std::vector<CoreTicks> cpu_history;
  std::vector<double> cpu_load_percentages;
  std::vector<NetInterfaceData> active_network_speeds;
  std::vector<ProcessInfo> system_process_tree;

  // Execute initial priming parse cycle to eliminate delta boundary zero
  // anomalies
  metrics_provider.fetch_network_speeds(active_network_speeds);

  // Spawn the standalone input worker loop thread execution block
  std::thread keyboard_thread(worker_keyboard_listener,
                              std::ref(terminal_controller));

  // Core Engine Frame Rendering Loop
  while (g_is_running) {
    const std::string hardware_banner = SysInfo::get_static_sys_string();
    const std::string scheduler_load = SysInfo::get_load_avg_string();

    SysInfo::fetch_cpu_percentages(cpu_history, cpu_load_percentages);
    const RamMetrics volatile_ram = SysInfo::fetch_ram_metrics();

    metrics_provider.fetch_network_speeds(active_network_speeds);
    const std::vector<DiskMetrics> storage_volumes =
        SysInfo::fetch_disk_metrics();
    metrics_provider.fetch_process_list(system_process_tree);

    frame_renderer.render(hardware_banner, scheduler_load, cpu_load_percentages,
                          volatile_ram, active_network_speeds, storage_volumes,
                          system_process_tree);

    // Dynamic split frame sampling interval calculation
    std::unique_lock<std::mutex> lock(g_shutdown_mutex);
    if (g_shutdown_cv.wait_for(lock, std::chrono::milliseconds(500),
                               [] { return !g_is_running.load(); })) {
      break;
    }
  }

  // Gracefully join and synchronize background worker resources
  if (keyboard_thread.joinable()) {
    keyboard_thread.join();
  }

  // Reset text matrix cleanly before dropping back into normal command line
  // shells
  std::cout << "\033[2J\033[1;1H" << "Exited.\n";
  return 0;
}
