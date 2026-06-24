/**
 * @file ui_renderer.hpp
 * @brief UI engine declarations processing responsive visual splits.
 * @author Kevin Rozario
 * @license MIT
 */

#pragma once
#include "sys_info.hpp"
#include <string>
#include <vector>

/**
 * @class UiRenderer
 * @brief Formats raw system statistics data points into high-density TUI
 * screens.
 */
class UiRenderer {
private:
  int terminal_width = 80;  ///< Current horizontal grid width constraints
  int terminal_height = 24; ///< Current vertical grid row constraints

  void get_terminal_dimensions();
  std::string build_bar_graph(double percentage, int usable_width);
  std::string format_net_speed(double bytes_per_second);

public:
  /**
   * @brief Orchestrates and draws a complete visual layout iteration.
   * @param static_info Core hardware model parameters.
   * @param load_info Kernel task scheduler execution queues.
   * @param cpu_percentages Array containing calculations for individual cores.
   * @param ram Dynamic values detailing system RAM allocations.
   * @param networks Active network routing states.
   * @param disks Storage volumes mapping properties.
   * @param processes Sorted array showing process resource consumers.
   */
  void render(const std::string &static_info, const std::string &load_info,
              const std::vector<double> &cpu_percentages, const RamMetrics &ram,
              const std::vector<NetInterfaceData> &networks,
              const std::vector<DiskMetrics> &disks,
              const std::vector<ProcessInfo> &processes);
};
