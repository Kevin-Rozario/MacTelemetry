#include "../include/sys_info.hpp"
#include <algorithm>
#include <ctime>
#include <ifaddrs.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <unistd.h>

/**
 * @brief Instantiates the core provider subsystem and marks baseline epoch
 * parameters.
 */
SysInfo::SysInfo() { prev_net_time = std::chrono::steady_clock::now(); }

/**
 * @brief Pulls system identification strings natively via sysctl bindings.
 * @return String combining hardware model configurations and real-world system
 * times.
 */
std::string SysInfo::get_static_sys_string() {
  char model[128] = {0};
  size_t model_len = sizeof(model);
  sysctlbyname("hw.model", model, &model_len, nullptr, 0);

  char os_version[128] = {0};
  size_t os_len = sizeof(os_version);
  sysctlbyname("kern.osrelease", os_version, &os_len, nullptr, 0);

  std::time_t now = std::time(nullptr);
  char time_buf[64];
  std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S",
                std::localtime(&now));

  return " [ " + std::string(model) + " | Darwin " + std::string(os_version) +
         " ]   " + std::string(time_buf);
}

/**
 * @brief Queries standard BSD load averages showing job task queues.
 * @return String formatting tracking windows over 1, 5, and 15 minutes.
 */
std::string SysInfo::get_load_avg_string() {
  double load[3];
  if (getloadavg(load, 3) != -1) {
    char buf[64];
    snprintf(buf, sizeof(buf), "1m: %.2f | 5m: %.2f | 15m: %.2f", load[0],
             load[1], load[2]);
    return std::string(buf);
  }
  return "N/A";
}

/**
 * @brief Derives isolated usage load layers across independent hardware core
 * indexes.
 */
void SysInfo::fetch_cpu_percentages(std::vector<CoreTicks> &previous_snapshot,
                                    std::vector<double> &out_percentages) {
  processor_cpu_load_info_t cpu_load_array;
  mach_msg_type_number_t msg_count;
  natural_t core_count;

  kern_return_t kr = host_processor_info(
      mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &core_count,
      reinterpret_cast<processor_info_array_t *>(&cpu_load_array), &msg_count);
  if (kr != KERN_SUCCESS)
    return;

  if (previous_snapshot.size() != core_count)
    previous_snapshot.resize(core_count);
  out_percentages.resize(core_count);

  for (natural_t i = 0; i < core_count; ++i) {
    unsigned long long user = cpu_load_array[i].cpu_ticks[CPU_STATE_USER];
    unsigned long long sys = cpu_load_array[i].cpu_ticks[CPU_STATE_SYSTEM];
    unsigned long long nice = cpu_load_array[i].cpu_ticks[CPU_STATE_NICE];
    unsigned long long idle = cpu_load_array[i].cpu_ticks[CPU_STATE_IDLE];

    unsigned long long current_active = user + sys + nice;
    unsigned long long current_idle = idle;

    unsigned long long delta_active =
        current_active - previous_snapshot[i].active;
    unsigned long long delta_idle = current_idle - previous_snapshot[i].idle;
    unsigned long long delta_total = delta_active + delta_idle;

    double pct = (delta_total > 0)
                     ? (static_cast<double>(delta_active) / delta_total) * 100.0
                     : 0.0;
    out_percentages[i] = std::clamp(pct, 0.0, 100.0);

    previous_snapshot[i].active = current_active;
    previous_snapshot[i].idle = current_idle;
  }
  vm_deallocate(mach_task_self(),
                reinterpret_cast<vm_address_t>(cpu_load_array),
                sizeof(processor_cpu_load_info_t) * core_count);
}

/**
 * @brief Maps native 64-bit Mach page allocations to calculate volatile RAM
 * states.
 */
RamMetrics SysInfo::fetch_ram_metrics() {
  RamMetrics metrics;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_statistics64_data_t vm_stats;

  if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                        reinterpret_cast<host_info64_t>(&vm_stats),
                        &count) != KERN_SUCCESS) {
    return metrics;
  }

  vm_size_t page_size;
  host_page_size(mach_host_self(), &page_size);

  uint64_t total_memory = 0;
  size_t len = sizeof(total_memory);
  int mib[] = {CTL_HW, HW_MEMSIZE};
  sysctl(mib, 2, &total_memory, &len, nullptr, 0);

  uint64_t wired = static_cast<uint64_t>(vm_stats.wire_count) * page_size;
  uint64_t active = static_cast<uint64_t>(vm_stats.active_count) * page_size;
  uint64_t compressed =
      static_cast<uint64_t>(vm_stats.compressor_page_count) * page_size;

  uint64_t used_bytes = wired + active + compressed;

  metrics.used_gb =
      static_cast<double>(used_bytes) / (1024.0 * 1024.0 * 1024.0);
  metrics.total_gb =
      static_cast<double>(total_memory) / (1024.0 * 1024.0 * 1024.0);
  metrics.usage_percentage =
      (total_memory > 0)
          ? (static_cast<double>(used_bytes) / total_memory) * 100.0
          : 0.0;

  return metrics;
}

/**
 * @brief Captures link-layer network speeds using low-level BSD getifaddrs
 * allocations.
 */
void SysInfo::fetch_network_speeds(
    std::vector<NetInterfaceData> &out_interfaces) {
  struct ifaddrs *ifap = nullptr;
  if (getifaddrs(&ifap) != 0)
    return;

  auto now = std::chrono::steady_clock::now();
  double interval_seconds =
      std::chrono::duration<double>(now - prev_net_time).count();
  if (interval_seconds <= 0.0)
    interval_seconds = 0.5;

  std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> curr_bytes;

  for (struct ifaddrs *ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_LINK)
      continue;

    struct if_data *ifd = reinterpret_cast<struct if_data *>(ifa->ifa_data);
    if (ifd == nullptr)
      continue;

    std::string if_name(ifa->ifa_name);
    // Focus purely on your Mac's core hardware layout
    if (if_name != "en0" && if_name != "lo0" && if_name.rfind("utun", 0) != 0)
      continue;

    curr_bytes[if_name].first += ifd->ifi_ibytes;
    curr_bytes[if_name].second += ifd->ifi_obytes;
  }
  freeifaddrs(ifap);

  out_interfaces.clear();
  for (const auto &[name, bytes] : curr_bytes) {
    if (prev_net_bytes.count(name)) {
      uint64_t delta_in = (bytes.first >= prev_net_bytes[name].first)
                              ? (bytes.first - prev_net_bytes[name].first)
                              : 0;
      uint64_t delta_out = (bytes.second >= prev_net_bytes[name].second)
                               ? (bytes.second - prev_net_bytes[name].second)
                               : 0;

      NetInterfaceData data;
      data.name = name;
      // Capture raw absolute bytes transferred in this specific slice
      data.down_speed_bytes = static_cast<double>(delta_in) / interval_seconds;
      data.up_speed_bytes = static_cast<double>(delta_out) / interval_seconds;

      // Keep your screen clean by displaying only active interfaces (or your
      // primary Wi-Fi card)
      if (data.down_speed_bytes > 0 || data.up_speed_bytes > 0 ||
          name == "en0") {
        out_interfaces.push_back(data);
      }
    }
  }

  if (out_interfaces.empty()) {
    out_interfaces.push_back(NetInterfaceData{"en0", 0.0, 0.0});
  }

  // Sort primary routes to ensure interface ordering doesn't flicker
  std::sort(out_interfaces.begin(), out_interfaces.end(),
            [](const NetInterfaceData &a, const NetInterfaceData &b) {
              return a.name < b.name;
            });

  prev_net_bytes = curr_bytes;
  prev_net_time = now;
}

/**
 * @brief Scans active mounted local APFS filesystems using getmntinfo
 * configurations.
 */
std::vector<DiskMetrics> SysInfo::fetch_disk_metrics() {
  std::vector<DiskMetrics> out_disks;
  struct statfs *mounts = nullptr;
  int num_mounts = getmntinfo(&mounts, MNT_NOWAIT);

  if (num_mounts <= 0)
    return out_disks;

  for (int i = 0; i < num_mounts; ++i) {
    std::string flags(mounts[i].f_fstypename);
    std::string path(mounts[i].f_mntonname);

    if (flags != "apfs" && flags != "hfs")
      continue;
    if (path.rfind("/System/Volumes/Data", 0) == 0 || path == "/") {
      if (path == "/System/Volumes/Data")
        path = "Macintosh HD";
      if (path == "/")
        continue;

      DiskMetrics d;
      d.mount_point = path;

      uint64_t total_bytes = mounts[i].f_blocks * mounts[i].f_bsize;
      uint64_t free_bytes = mounts[i].f_bavail * mounts[i].f_bsize;

      d.total_gb =
          static_cast<double>(total_bytes) / (1024.0 * 1024.0 * 1024.0);
      d.used_gb = static_cast<double>(total_bytes - free_bytes) /
                  (1024.0 * 1024.0 * 1024.0);
      d.usage_percentage =
          (total_bytes > 0)
              ? (static_cast<double>(total_bytes - free_bytes) / total_bytes) *
                    100.0
              : 0.0;

      out_disks.push_back(d);
    }
  }
  return out_disks;
}

/**
 * @brief Traverses active task trees via libproc to extract performance
 * numbers.
 */
void SysInfo::fetch_process_list(std::vector<ProcessInfo> &out_processes) {
  out_processes.clear();
  int pid_count = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
  if (pid_count <= 0)
    return;

  std::vector<pid_t> pids(pid_count);
  pid_count =
      proc_listpids(PROC_ALL_PIDS, 0, pids.data(), pid_count * sizeof(pid_t));

  std::unordered_map<pid_t, uint64_t> curr_proc_time;
  uint64_t current_total_system_time = 0;

  for (int i = 0; i < pid_count; ++i) {
    if (pids[i] <= 0)
      continue;
    struct proc_taskinfo task;
    if (proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &task, sizeof(task)) ==
        sizeof(task)) {
      uint64_t total_time = task.pti_total_user + task.pti_total_system;
      curr_proc_time[pids[i]] = total_time;
      current_total_system_time += total_time;
    }
  }

  uint64_t system_delta = current_total_system_time - prev_total_system_time;

  for (int i = 0; i < pid_count; ++i) {
    if (pids[i] <= 0)
      continue;
    struct proc_bsdinfo bsd;
    struct proc_taskinfo task;

    if (proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &bsd, sizeof(bsd)) ==
            sizeof(bsd) &&
        proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &task, sizeof(task)) ==
            sizeof(task)) {

      ProcessInfo p;
      p.pid = pids[i];
      p.name = bsd.pbi_name;
      p.mem_gb = static_cast<double>(task.pti_resident_size) /
                 (1024.0 * 1024.0 * 1024.0);

      if (bsd.pbi_status == SRUN)
        p.state = "R";
      else if (bsd.pbi_status == SSLEEP)
        p.state = "S";
      else if (bsd.pbi_status == SSTOP)
        p.state = "T";
      else if (bsd.pbi_status == SZOMB)
        p.state = "Z";
      else
        p.state = "I";

      if (prev_proc_time.count(p.pid) && system_delta > 0) {
        uint64_t proc_delta =
            (curr_proc_time[p.pid] >= prev_proc_time[p.pid])
                ? (curr_proc_time[p.pid] - prev_proc_time[p.pid])
                : 0;
        p.cpu_pct = (static_cast<double>(proc_delta) / system_delta) * 100.0 *
                    sysconf(_SC_NPROCESSORS_ONLN);
      } else {
        p.cpu_pct = 0.0;
      }

      out_processes.push_back(p);
    }
  }

  std::sort(out_processes.begin(), out_processes.end(),
            [](const ProcessInfo &a, const ProcessInfo &b) {
              return a.cpu_pct > b.cpu_pct;
            });

  prev_proc_time = curr_proc_time;
  prev_total_system_time = current_total_system_time;
}
