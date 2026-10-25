#include "src/core/diagnostics/npu.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string_view>

namespace gufo::diagnostics {

namespace {

std::string ReadUeventField(const std::filesystem::path& uevent,
                            std::string_view key) {
  std::ifstream file(uevent);
  std::string line;
  while (std::getline(file, line)) {
    if (line.starts_with(key) && line.size() > key.size() &&
        line[key.size()] == '=') {
      return line.substr(key.size() + 1);
    }
  }
  return {};
}

}  // namespace

std::vector<NpuDevice> QueryNpuDevices(const std::filesystem::path& sys_root,
                                       const std::filesystem::path& dev_root) {
  std::vector<NpuDevice> devices;
  std::error_code ec;
  const auto accel_dir = sys_root / "class" / "accel";
  if (!std::filesystem::exists(accel_dir, ec)) {
    return devices;
  }
  for (const auto& entry : std::filesystem::directory_iterator(accel_dir, ec)) {
    if (ec) {
      break;
    }
    const std::string name = entry.path().filename().string();
    constexpr std::string_view kPrefix = "accel";
    if (!name.starts_with(kPrefix) || name.size() == kPrefix.size() ||
        !std::all_of(name.begin() + kPrefix.size(), name.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
      continue;
    }
    const auto uevent = entry.path() / "device" / "uevent";
    NpuDevice device;
    device.name = name;
    device.driver = ReadUeventField(uevent, "DRIVER");
    device.pci_id = ReadUeventField(uevent, "PCI_ID");
    device.pci_slot = ReadUeventField(uevent, "PCI_SLOT_NAME");
    const auto node = dev_root / "accel" / name;
    device.node_path = node.string();
    device.node_exists = std::filesystem::exists(node, ec);
    if (device.node_exists) {
      std::ifstream probe(node);
      device.node_accessible = probe.is_open();
    }
    devices.push_back(std::move(device));
  }
  std::sort(
      devices.begin(), devices.end(),
      [](const NpuDevice& a, const NpuDevice& b) { return a.name < b.name; });
  return devices;
}

}  // namespace gufo::diagnostics
