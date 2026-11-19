/**
 * @file src/optimise.h
 * @brief Encoder test for the host's own preset (roadmap N6): encodes the moving test picture
 *        with each preset step and picks the highest quality that keeps up.
 */
#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace optimise {
  /**
   * @brief One preset step, measured.
   */
  struct result_t {
    nlohmann::json value;  ///< The preset value ("speed", 4, "veryfast", ...).
    int frames = 0;  ///< Frames encoded.
    double avg_ms = 0;  ///< Average time to convert and encode a frame.
    double p95_ms = 0;  ///< 95th percentile of the same.
    std::string error;  ///< Why it couldn't be measured, if it couldn't.
  };

  /// The share of a frame interval a preset's 95th percentile may take to count as keeping up.
  constexpr double frame_share = 0.4;

  /**
   * @brief The preset to recommend from the measured steps (fastest first).
   * @param results The measured steps, in the order of the encoder's preset scale.
   * @param quality_ceiling Index of the highest step that may be recommended.
   * @param frame_ms The frame interval measured for (16.7 at 60 fps).
   * @return Index into results: the highest-quality step up to the ceiling that keeps up,
   *         else the fastest one that was measured; nothing if none was.
   */
  std::optional<std::size_t> pick(const std::vector<result_t> &results, std::size_t quality_ceiling, double frame_ms);

  /**
   * @brief Starts the test in the background.
   * @return 202 when started, 409 when a test or a stream runs, 503 without a working encoder.
   */
  int start(std::string &error);

  /// True while a test runs (streams aren't started meanwhile).
  bool running();

  /**
   * @brief GET /api/v1/optimise: the test's state and its last result.
   * @details {"state": "idle"|"running"|"done"|"failed", "progress": 0-100, "error",
   *          "encoder", "codec", "width", "height", "fps", "setting", "current",
   *          "results": [{"value", "frames", "avg_ms", "p95_ms", "error"}], "pick"}
   */
  nlohmann::json describe();

  /**
   * @brief POST /api/v1/optimise/apply: writes the picked preset to the config, kept for undo
   *        with the recommendations (POST /api/v1/recommendations/rollback undoes it).
   * @return HTTP status and body.
   */
  std::pair<int, nlohmann::json> apply(const std::string &config_file);
}  // namespace optimise
