#pragma once
#include <string>
namespace nova {
struct Input {
  std::string key, text;
  int x = 0, y = 0, wheel = 0;
  bool mouse = false, resize = false;
};
}
