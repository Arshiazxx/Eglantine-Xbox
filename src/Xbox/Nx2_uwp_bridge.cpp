// SPDX-License-Identifier: GPL-3.0-or-later
// Target Path in Repo: src/xbox/nx2_uwp_bridge.cpp
// Eglantine (nx2 branch) Windows CLI to Xbox Dev Mode UWP Headless Bridge.

#include "nx2_uwp_bridge.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>
#include <windows.h>

#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/loader/loader.h"

namespace Nx2Xbox {

namespace {

constexpr size_t kMaxLogLines = 5000;
std::mutex g_log_mutex;
std::deque<std::string> g_log_lines;
std::atomic<bool> g_is_running{false};
std::function<void(const std::string&)> g_log_callback = nullptr;

// Headless EmuWindow implementation for Xbox Dev Mode.
// Allows Core::System to initialize and run guest CPU/kernel execution without requiring
// a Vulkan or DirectX window surface.
class EmuWindow_Xbox_Headless final : public Core::Frontend::EmuWindow {
public:
    EmuWindow_Xbox_Headless() {
        window_info.type = Core::Frontend::WindowSystemType::Headless;
    }
    ~EmuWindow_Xbox_Headless() override = default;

    void OnFrameDisplayed() override {}

    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return nullptr;
    }

    bool IsShown() const override {
        return true;
    }
};

} // anonymous namespace

void AppendLog(const std::string& category, const std::string& message) {
    std::string formatted = "[" + category + "] " + message + "\n";
    OutputDebugStringA(formatted.c_str());

    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        if (g_log_lines.size() >= kMaxLogLines) {
            g_log_lines.pop_front();
        }
        g_log_lines.push_back("[" + category + "] " + message);
    }

    if (g_log_callback) {
        g_log_callback(formatted);
    }
}

std::string GetLogBuffer() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::ostringstream oss;
    for (const auto& line : g_log_lines) {
        oss << line << '\n';
    }
    return oss.str();
}

void ClearLogBuffer() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log_lines.clear();
}

bool IsRunning() {
    return g_is_running.load();
}

std::string ResolveLooseExeFs(const std::string& folder_or_file_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path input(folder_or_file_path);

    if (fs::is_directory(input, ec)) {
        fs::path main_bin = input / "main";
        fs::path npdm_bin = input / "main.npdm";

        if (fs::exists(main_bin, ec)) {
            AppendLog("NX2-Loader", "Found loose ExeFS directory: " + input.string() +
                      (fs::exists(npdm_bin, ec) ? " (with main.npdm)" : " (main only)"));
            return main_bin.string();
        }
    }
    return folder_or_file_path;
}

int RunLooseExeFsHeadless(const std::string& exefs_dir_path,
                         std::function<void(const std::string&)> log_callback) {
    if (g_is_running.exchange(true)) {
        AppendLog("NX2-Boot", "Error: A session is already active.");
        return -1;
    }

    g_log_callback = log_callback;
    ClearLogBuffer();
    AppendLog("NX2-Boot", "=== Eglantine NX2 Xbox Dev Mode Kernel Boot ===");
    AppendLog("NX2-Boot", "Input Path: " + exefs_dir_path);

    std::string boot_binary = ResolveLooseExeFs(exefs_dir_path);
    AppendLog("NX2-Boot", "Target Binary: " + boot_binary);

    try {
        auto& system = Core::System::GetInstance();
        system.Initialize();

        auto headless_window = std::make_unique<EmuWindow_Xbox_Headless>();
        AppendLog("NX2-Boot", "Invoking Core::System::Load on Xbox runtime...");

        const auto load_result = system.Load(*headless_window, boot_binary);
        if (load_result != Core::SystemResultStatus::Success) {
            AppendLog("NX2-Boot", "Load failed with status: " +
                      std::to_string(static_cast<int>(load_result)));
            g_is_running.store(false);
            return static_cast<int>(load_result);
        }

        AppendLog("NX2-Boot", "Load succeeded! Executing guest CPU/Kernel cycles...");
        system.Run();

        while (g_is_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        AppendLog("NX2-Boot", "Stopping Core::System execution...");
        system.Pause();
        system.ShutdownMainProcess();
    } catch (const std::exception& ex) {
        AppendLog("NX2-Exception", ex.what());
        g_is_running.store(false);
        return -2;
    }

    g_is_running.store(false);
    AppendLog("NX2-Boot", "=== Session Terminated Cleanly ===");
    return 0;
}

void StopSession() {
    g_is_running.store(false);
}

} // namespace Nx2Xbox