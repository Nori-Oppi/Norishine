/**
 * @file src/host_stats.cpp
 * @brief Definitions for host load figures (CPU, memory, GPU) shown by the management API.
 */
// standard includes
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>

// platform includes
#ifdef _WIN32
  #include <windows.h>
  #include <pdh.h>
  #include <pdhmsg.h>
  #include <psapi.h>
#endif

// local includes
#include "host_stats.h"
#include "logging.h"

using namespace std::literals;

namespace host_stats {
  std::optional<double> cpu_percent(const cpu_times_t &before, const cpu_times_t &after) {
    if (after.total <= before.total || after.idle < before.idle) {
      return std::nullopt;
    }
    auto total = static_cast<double>(after.total - before.total);
    auto idle = static_cast<double>(after.idle - before.idle);
    return std::clamp((total - idle) / total * 100, 0.0, 100.0);
  }

  std::pair<std::optional<double>, std::optional<double>> gpu_load(const std::vector<std::pair<std::string, double>> &instances) {
    // Each instance is one process's use of one engine; an engine's load is the sum over
    // processes. The name after "pid_N_" identifies the engine.
    std::map<std::string, double> engines;
    for (const auto &[name, value] : instances) {
      auto luid = name.find("luid_");
      if (luid == std::string::npos || !std::isfinite(value)) {
        continue;
      }
      engines[name.substr(luid)] += value;
    }

    std::optional<double> gpu;
    std::optional<double> encoder;
    for (const auto &[engine, value] : engines) {
      auto load = std::clamp(value, 0.0, 100.0);
      gpu = std::max(gpu.value_or(0), load);
      auto type = engine.find("engtype_");
      if (type != std::string::npos) {
        auto name = engine.substr(type + 8);
        // NVIDIA and Intel report "VideoEncode"; AMD's VCN reports "VideoCodec" (encode and decode).
        if (name.find("Encode") != std::string::npos || name.find("Codec") != std::string::npos) {
          encoder = std::max(encoder.value_or(0), load);
        }
      }
    }
    return {gpu, encoder};
  }

  namespace {
    // Divides by a whole number, so 1.9 stays 1.9 in the JSON (not 1.9000000000000001).
    double round_to(double value, int decimals) {
      double factor = std::pow(10.0, decimals);
      return std::round(value * factor) / factor;
    }

#ifdef _WIN32
    std::uint64_t to_u64(const FILETIME &t) {
      return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    }

    struct sample_t {
      std::chrono::steady_clock::time_point at;
      std::optional<cpu_times_t> system;
      std::optional<std::uint64_t> process;  ///< Kernel and user time, 100 ns units.
    };

    sample_t take_sample() {
      sample_t sample {std::chrono::steady_clock::now()};
      FILETIME idle, kernel, user;
      if (GetSystemTimes(&idle, &kernel, &user)) {
        // Kernel time includes idle time.
        sample.system = cpu_times_t {to_u64(idle), to_u64(kernel) + to_u64(user)};
      }
      FILETIME created, exited;
      if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        sample.process = to_u64(kernel) + to_u64(user);
      }
      return sample;
    }

    /**
     * @brief GPU engine counters. PDH needs two collections for a rate, so the query stays open.
     */
    class gpu_query_t {
    public:
      gpu_query_t() {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
          query = nullptr;
          return;
        }
        if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
          BOOST_LOG(debug) << "Host stats: GPU engine counters are not available"sv;
          PdhCloseQuery(query);
          query = nullptr;
          return;
        }
        PdhCollectQueryData(query);
      }

      ~gpu_query_t() {
        if (query) {
          PdhCloseQuery(query);
        }
      }

      gpu_query_t(const gpu_query_t &) = delete;
      gpu_query_t &operator=(const gpu_query_t &) = delete;

      std::pair<std::optional<double>, std::optional<double>> collect() {
        if (!query || PdhCollectQueryData(query) != ERROR_SUCCESS) {
          return {};
        }
        DWORD size = 0;
        DWORD count = 0;
        auto status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, nullptr);
        if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || size == 0) {
          return {};
        }
        std::vector<std::uint8_t> buffer(size);
        auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items) != ERROR_SUCCESS) {
          return {};
        }
        std::vector<std::pair<std::string, double>> instances;
        instances.reserve(count);
        for (DWORD i = 0; i < count; ++i) {
          if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) {
            continue;
          }
          // Instance names are ASCII.
          std::string name;
          for (auto *c = items[i].szName; c && *c; ++c) {
            name.push_back(static_cast<char>(*c));
          }
          instances.emplace_back(std::move(name), items[i].FmtValue.doubleValue);
        }
        return gpu_load(instances);
      }

    private:
      PDH_HQUERY query = nullptr;
      PDH_HCOUNTER counter = nullptr;
    };

    class sampler_t {
    public:
      stats_t current() {
        std::lock_guard lg {mutex};
        auto now = std::chrono::steady_clock::now();
        // Readings closer together than this are mostly noise: answer the last result.
        if (last && now - last->at < 250ms) {
          return result;
        }
        if (!last || now - last->at > 30s) {
          // Nothing recent to compare with: take a short first reading.
          last = take_sample();
          gpu.collect();
          std::this_thread::sleep_for(250ms);
        }

        auto sample = take_sample();
        stats_t stats;
        if (last->system && sample.system) {
          stats.cpu_percent = cpu_percent(*last->system, *sample.system);
        }
        if (last->process && sample.process && sample.process >= last->process) {
          auto wall = std::chrono::duration_cast<std::chrono::duration<double, std::ratio<1, 10'000'000>>>(sample.at - last->at).count();
          auto cpus = std::max(1u, std::thread::hardware_concurrency());
          if (wall > 0) {
            stats.process_cpu_percent = std::clamp(static_cast<double>(*sample.process - *last->process) / (wall * cpus) * 100, 0.0, 100.0);
          }
        }

        MEMORYSTATUSEX memory {sizeof(memory)};
        if (GlobalMemoryStatusEx(&memory)) {
          stats.memory_total_mb = memory.ullTotalPhys / (1024 * 1024);
          stats.memory_used_mb = (memory.ullTotalPhys - memory.ullAvailPhys) / (1024 * 1024);
        }
        PROCESS_MEMORY_COUNTERS counters {};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
          stats.process_memory_mb = counters.WorkingSetSize / (1024 * 1024);
        }
        std::tie(stats.gpu_percent, stats.encoder_percent) = gpu.collect();

        last = sample;
        result = stats;
        result_at = now;
        return stats;
      }

      std::optional<stats_t> latest(std::chrono::steady_clock::duration max_age) {
        std::lock_guard lg {mutex};
        if (!result_at || std::chrono::steady_clock::now() - *result_at > max_age) {
          return std::nullopt;
        }
        return result;
      }

    private:
      std::mutex mutex;
      std::optional<sample_t> last;
      stats_t result;
      std::optional<std::chrono::steady_clock::time_point> result_at;
      gpu_query_t gpu;
    };
#endif
  }  // namespace

#ifdef _WIN32
  namespace {
    sampler_t &sampler() {
      static sampler_t instance;
      return instance;
    }
  }  // namespace
#endif

  stats_t current() {
#ifdef _WIN32
    return sampler().current();
#else
    return {};
#endif
  }

  std::optional<stats_t> latest(std::chrono::steady_clock::duration max_age) {
#ifdef _WIN32
    return sampler().latest(max_age);
#else
    return std::nullopt;
#endif
  }

  nlohmann::json to_json(const stats_t &s) {
    auto percent = [](const std::optional<double> &v) -> nlohmann::json {
      return v ? nlohmann::json(round_to(*v, 1)) : nlohmann::json(nullptr);
    };
    auto mb = [](const std::optional<std::uint64_t> &v) -> nlohmann::json {
      return v ? nlohmann::json(*v) : nlohmann::json(nullptr);
    };
    return {
      {"cpu_percent", percent(s.cpu_percent)},
      {"process_cpu_percent", percent(s.process_cpu_percent)},
      {"memory_total_mb", mb(s.memory_total_mb)},
      {"memory_used_mb", mb(s.memory_used_mb)},
      {"process_memory_mb", mb(s.process_memory_mb)},
      {"gpu_percent", percent(s.gpu_percent)},
      {"encoder_percent", percent(s.encoder_percent)},
    };
  }
}  // namespace host_stats
