#pragma once

namespace uirepo::fo4::PackageHostStartup {

// Scans <plugin folder>/UIRepo/packages once per process; later calls do nothing. Never throws.
void run() noexcept;

} // namespace uirepo::fo4::PackageHostStartup
