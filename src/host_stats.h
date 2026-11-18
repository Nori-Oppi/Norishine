/**
 * @file src/host_stats.h
 * @brief Declarations for host load figures (CPU, memory, GPU) shown by the management API.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// lib includes
#include <nlohmann/json.hpp>

namespace host_stats {
  /**
   * @brief Host load. Every figure is optional: it is empty where the platform can't
   * report it (only Windows reports all of them).
   */
  struct stats_t {
    std::optional<double> cpu_percent;  ///< All processes, all cores.
    std::optional<double> process_cpu_percent;  ///< Norishine itself, share of all cores.
    std::optional<std::uint64_t> memory_total_mb;
    std::optional<std::uint64_t> memory_used_mb;
    std::optional<std::uint64_t> process_memory_mb;  ///< Norishine's working set.
    std::optional<double> gpu_percent;  ///< The busiest GPU engine, as Task Manager shows it.
    std::optional<double> encoder_percent;  ///< The busiest video encode engine.
  };

  /**
   * @brief CPU time counters at one moment, in any unit.
   */
  struct cpu_times_t {
    std::uint64_t idle = 0;
    std::uint64_t total = 0;  ///< Idle included.
  };

  /// Share of time not idle between two readings; empty when no time passed.
  std::optional<double> cpu_percent(const cpu_times_t &before, const cpu_times_t &after);

  /**
   * @brief GPU load from the "GPU Engine(*)\Utilization Percentage" counter instances.
   * @param instances Instance name (pid_..._luid_..._phys_N_eng_N_engtype_X) and value.
   * @return Busiest engine overall, and busiest video encode engine (empty if there is none).
   */
  std::pair<std::optional<double>, std::optional<double>> gpu_load(const std::vector<std::pair<std::string, double>> &instances);

  /// Current host load. The first call takes about a quarter of a second.
  stats_t current();

  /// The last reading current() took, if it is younger than max_age; never waits.
  std::optional<stats_t> latest(std::chrono::steady_clock::duration max_age);

  nlohmann::json to_json(const stats_t &stats);
}  // namespace host_stats
