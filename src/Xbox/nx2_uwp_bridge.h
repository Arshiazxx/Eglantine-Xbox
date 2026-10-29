// SPDX-License-Identifier: GPL-3.0-or-later
// Target Path in Repo: src/xbox/nx2_uwp_bridge.h
// Eglantine (nx2 branch) Windows CLI to Xbox Dev Mode UWP Headless Bridge.

#pragma once

#include <string>
#include <functional>

namespace Nx2Xbox {

// Appends a formatted log message to the thread-safe buffer and OutputDebugString
void AppendLog(const std::string& category, const std::string& message);

// Returns all kernel/loader log lines buffered since the last clear
std::string GetLogBuffer();

// Clears the buffered logs
void ClearLogBuffer();

// Checks if a background NX2 execution session is currently active
bool IsRunning();

// Resolves a folder path containing loose ExeFS files ('main' and 'main.npdm')
std::string ResolveLooseExeFs(const std::string& folder_or_file_path);

// Starts a headless NX2 kernel boot session on a background worker thread.
// progress_callback is called whenever new log output is available.
int RunLooseExeFsHeadless(const std::string& exefs_dir_path,
                         std::function<void(const std::string&)> log_callback = nullptr);

// Cleanly signals the active NX2 session to stop
void StopSession();

} // namespace Nx2Xbox