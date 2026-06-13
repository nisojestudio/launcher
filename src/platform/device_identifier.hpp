#pragma once

#include <string>

namespace nlp3::platform {

/// Returns a composite device identifier: "persistent-uuid | machine-guid"
///
/// - persistent-uuid: UUID v4 generated on first run, stored in %LOCALAPPDATA%\NisojeStudio\.device-id.
///   Survives reinstalls and updates because AppData is never touched.
/// - machine-guid: MachineGUID from HKLM\SOFTWARE\Microsoft\Cryptography.
///   Does not change even if AppData is deleted.
///
/// The composite ensures tracking survives local data loss while remaining
/// privacy-conscious (no MAC/hardware serials).
std::string get_composite_device_id();

/// Returns the human-readable device name (e.g. "Windows · es-419").
std::string get_device_name();

/// Returns the directory where persistent panel data is stored.
std::string get_panel_data_directory();

} // namespace nlp3::platform
