/**
 * @file tests/unit/test_permissions.cpp
 * @brief Test the client permission bits in src/crypto.h.
 */
#include "../tests_common.h"

#include <src/crypto.h>

using crypto::PERM;

TEST(PermissionTests, ManagementIsNeverADefault) {
  // Management through /api/v1 is granted only per device, from the host's web UI.
  EXPECT_TRUE(!(PERM::_default & PERM::_all_manage));
  EXPECT_TRUE(!(PERM::_first_device & PERM::_all_manage));
}

TEST(PermissionTests, StoredManagementBitsSurviveLoading) {
  // nvhttp masks stored permissions with _all when loading the state file.
  EXPECT_EQ(PERM::_all & PERM::manage_read, PERM::manage_read);
  EXPECT_EQ(PERM::_all & PERM::manage_write, PERM::manage_write);
}

TEST(PermissionTests, BitValuesMatchTheWebUi) {
  // src_assets/common/assets/web/pin.html permissionMapping uses the same numbers.
  EXPECT_EQ(static_cast<uint32_t>(PERM::manage_read), 0x10000000u);
  EXPECT_EQ(static_cast<uint32_t>(PERM::manage_write), 0x20000000u);
  EXPECT_EQ(static_cast<uint32_t>(PERM::_all), 0x371F1F00u);
}
