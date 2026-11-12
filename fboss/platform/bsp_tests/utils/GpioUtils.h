#pragma once

#include <string>
#include <vector>

namespace facebook::fboss::platform::bsp_tests {

// Represents a GPIO device detected by gpiodetect
struct GpioDetectResult {
  std::string name;
  std::string label;
  int lines;
};

// Represents a GPIO line info from gpioinfo
struct GpioInfoResult {
  std::string name;
  int lineNum;
  std::string direction;
};

class GpioUtils {
 public:
  // List GPIO chips via libgpiod, like the gpiodetect CLI
  // If label is provided, only return devices with matching label
  static std::vector<GpioDetectResult> gpiodetect(
      const std::string& label = "");

  // List the lines of a GPIO chip via libgpiod, like the gpioinfo CLI.
  // Unnamed lines are reported as "unnamed".
  static std::vector<GpioInfoResult> gpioinfo(const std::string& name);

  // Get the value of a specific GPIO line
  static int gpioget(const std::string& name, int line);
};

} // namespace facebook::fboss::platform::bsp_tests
