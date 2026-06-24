/**
 * @file sys_info.hpp
 * @brief Declarations for low-level macOS Mach kernel and BSD telemetry
 * metrics.
 * @author Kevin Rozario
 * @license MIT
 */

#pragma once
#include <chrono>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

/**
 * @struct CoreTicks
 * @brief Holds hardware cumulative tick counters for a logical CPU core.
 */
struct CoreTicks {
  unsigned long long active = 0; ///< Sum of user, system, and nice states
  unsigned long long idle = 0;   ///< Idle state ticks
};

/**
 * @struct RamMetrics
 * @brief Holds physical and virtual memory allocation parameters in Gigabytes.
 */
struct RamMetrics {
  double used_gb = 0.0;          ///< Total compressed, active, and wired memory
  double total_gb = 0.0;         ///< Total physical RAM installed
  double usage_percentage = 0.0; ///< Current saturation percentage of the RAM
};

/**
 * @struct NetInterfaceData
 * @brief Holds statistical byte transfer metrics for a network interface.
 */
struct NetInterfaceData {
  std::string name;              ///< Interface system moniker (e.g., "en0")
  double down_speed_bytes = 0.0; ///< Instantaneous download velocity in Bytes/s
  double up_speed_bytes = 0.0;   ///< Instantaneous upload velocity in Bytes/s
};

/**
 * @struct DiskMetrics
 * @brief Holds mounting point and filesystem storage data properties.
 */
struct DiskMetrics {
  std::string mount_point;       ///< Local mounting path (e.g., "Macintosh HD")
  double used_gb = 0.0;          ///< Used storage room in Gigabytes
  double total_gb = 0.0;         ///< Total storage container size in Gigabytes
  double usage_percentage = 0.0; ///< Percentage of storage space consumed
};

/**
 * @struct ProcessInfo
 * @brief Holds performance telemetry snap snapshots for a running process.
 */
struct ProcessInfo {
  pid_t pid = 0;        ///< Unique process identifier
  std::string name;     ///< Executable binary name
  double cpu_pct = 0.0; ///< Proportional CPU usage scaled by system core count
  double mem_gb = 0.0;  ///< Resident memory footprint size in Gigabytes
  std::string state;    ///< Canonical execution state code (R, S, T, Z, I)
};

/**
 * @class SysInfo
 * @brief Hardware analytics provider that maps native Apple kernel data layers.
 */
class SysInfo {
private:
  std::unordered_map<std::string, std::pair<uint64_t, uint64_t>>
      prev_net_bytes; ///< Historical I/O bytes
  std::chrono::steady_clock::time_point
      prev_net_time; ///< Previous execution delta timestamp
  std::unordered_map<pid_t, uint64_t>
      prev_proc_time;                  ///< Process running ticks archive
  uint64_t prev_total_system_time = 0; ///< Historical sum of all process cycles

public:
  SysInfo();
  static std::string get_static_sys_string();
  static std::string get_load_avg_string();
  static void fetch_cpu_percentages(std::vector<CoreTicks> &previous_snapshot,
                                    std::vector<double> &out_percentages);
  static RamMetrics fetch_ram_metrics();
  void fetch_network_speeds(std::vector<NetInterfaceData> &out_interfaces);
  static std::vector<DiskMetrics> fetch_disk_metrics();
  void fetch_process_list(std::vector<ProcessInfo> &out_processes);
};
