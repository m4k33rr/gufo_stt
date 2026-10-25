#include "src/core/diagnostics/npu.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "src/core/platform/os_info.hpp"

namespace {

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void Write(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path) << text;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("gufo-npu-" + std::to_string(gufo::platform::ProcessId()));
  std::filesystem::remove_all(root);
  const auto sys = root / "sys";
  const auto dev = root / "dev";

  Expect(gufo::diagnostics::QueryNpuDevices(sys, dev).empty(),
         "missing class/accel yields no devices");

  Write(sys / "class/accel/accel0/device/uevent",
        "DRIVER=amdxdna\nPCI_CLASS=118000\nPCI_ID=1022:17F0\n"
        "PCI_SLOT_NAME=0000:c4:00.1\n");
  Write(sys / "class/accel/accel0x", "ignored");
  Write(sys / "class/drm/card0/device/uevent", "DRIVER=amdgpu\n");

  auto devices = gufo::diagnostics::QueryNpuDevices(sys, dev);
  Expect(devices.size() == 1, "one accel device");
  Expect(devices[0].name == "accel0", "name");
  Expect(devices[0].driver == "amdxdna", "driver");
  Expect(devices[0].pci_id == "1022:17F0", "pci id");
  Expect(devices[0].pci_slot == "0000:c4:00.1", "pci slot");
  Expect(!devices[0].node_exists, "node absent before it is created");
  Expect(!devices[0].node_accessible, "absent node is not accessible");

  Write(dev / "accel/accel0", "");
  devices = gufo::diagnostics::QueryNpuDevices(sys, dev);
  Expect(devices[0].node_exists, "node present");
  Expect(devices[0].node_accessible, "node readable");

  std::filesystem::remove_all(root);
  std::cout << "npu_test OK\n";
  return 0;
}
