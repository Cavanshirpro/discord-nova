#pragma once
#include "media_config.hpp"
#include <string>
#ifdef _WIN32
#include <shellapi.h>
#else
#include <cerrno>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
extern char **environ;
#endif
namespace nova {
inline bool open_external(std::string value) {
#ifdef _WIN32
  auto path = media_example::path(value);
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
#ifdef __APPLE__
  std::string program = "open";
#else
  std::string program = "xdg-open";
#endif
  char* arguments[] = {program.data(), value.data(), nullptr};
  pid_t child = 0;
  if (posix_spawnp(&child, program.c_str(), nullptr, nullptr, arguments, environ) != 0) return false;
  std::thread([child] { while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {} }).detach();
  return true;
#endif
}
}
