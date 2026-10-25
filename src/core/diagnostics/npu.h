#ifndef GUFO_CORE_DIAGNOSTICS_NPU_H_
#define GUFO_CORE_DIAGNOSTICS_NPU_H_

#include <filesystem>
#include <string>
#include <vector>

namespace gufo::diagnostics {

/// One accelerator exposed by the kernel `accel` subsystem. On Strix Halo this
/// is the XDNA2 NPU driven by `amdxdna`.
struct NpuDevice {
  std::string name;       // "accel0"
  std::string driver;     // "amdxdna"
  std::string pci_id;     // "1022:17F0"
  std::string pci_slot;   // "0000:c4:00.1"
  std::string node_path;  // "/dev/accel/accel0"
  bool node_exists{false};
  bool node_accessible{false};  // read/write permitted for this user
};

/// Enumerates `<sys_root>/class/accel/accel*`. Device nodes are looked up under
/// `<dev_root>/accel/`. Returns an empty list when the host has no NPU driver.
[[nodiscard]] std::vector<NpuDevice> QueryNpuDevices(
    const std::filesystem::path& sys_root = "/sys",
    const std::filesystem::path& dev_root = "/dev");

}  // namespace gufo::diagnostics

#endif  // GUFO_CORE_DIAGNOSTICS_NPU_H_
