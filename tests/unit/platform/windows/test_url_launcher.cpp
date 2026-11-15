/**
 * @file tests/unit/platform/windows/test_url_launcher.cpp
 * @brief Test the command used to open URLs on Windows (Sunshine c8ea783c).
 */
#ifdef _WIN32
  #include "../../../tests_common.h"

  #include <src/platform/windows/misc.h>

TEST(UrlLaunchCommandTest, UsesCmdStartWithoutAutoRunOrDelayedExpansion) {
  const auto command = platf::make_url_launch_command(L"C:\\Windows\\System32\\cmd.exe", L"https://localhost:47990/");
  EXPECT_EQ(command, LR"(C:\Windows\System32\cmd.exe /d /v:off /c start "" https://localhost:47990/)");
}

TEST(UrlLaunchCommandTest, EscapesCmdMetacharactersInTheUrl) {
  // Unescaped, & would end the start command and run the rest as a second command.
  const auto command = platf::make_url_launch_command(L"C:\\Windows\\System32\\cmd.exe", L"https://localhost/?a=1&calc.exe");
  EXPECT_EQ(command, LR"(C:\Windows\System32\cmd.exe /d /v:off /c start "" https://localhost/?a=1^&calc.exe)");
}

TEST(UrlLaunchCommandTest, QuotesPathsWithSpaces) {
  const auto command = platf::make_url_launch_command(L"C:\\Program Files\\cmd.exe", L"https://localhost/a b");
  EXPECT_EQ(command, LR"("C:\Program Files\cmd.exe" /d /v:off /c start "" ^"https://localhost/a b^")");
}
#endif
