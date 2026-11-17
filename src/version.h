/**
 * @file src/version.h
 * @brief The build's version and commit.
 *
 * Defined in a file generated at configure time (src/version.cpp.in), not as compile
 * definitions: those change with every commit and would make every file recompile, so
 * no compiler cache could be reused between builds.
 */
#pragma once

namespace version {
  extern const char *const project;  ///< PROJECT_VERSION, e.g. "nightly-20261117-ec074d4"
  extern const char *const commit;  ///< The commit CI built, or empty.
}  // namespace version
