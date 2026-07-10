#include <windows.h>
#include <wininet.h>
#include <gdiplus.h>
#include <string>
#include <chrono>
#include <thread>
#include <filesystem>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
namespace fs = std::filesystem;

// =====================================================
// Constants
// =====================================================
const std::wstring APP_NAME = L"FY4B";
const std::wstring IMAGE_URL = L"https://img.nsmc.org.cn/CLOUDIMAGE/FY4B/AGRI/SWCI/FY4B_DISK_SWCI.JPG";
const int UPDATE_INTERVAL_SEC = 600; // 10 minutes

// Global paths
std::wstring INSTALL_DIR;
std::wstring IMAGE_FILE;
std::wstring BACKUP_IMAGE_FILE;

// =====================================================
// System Functions
// =====================================================

void nvGetScreenResolution(int& width, int& height) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
}

bool nvCheckNetwork() {
    HINTERNET hInternet = InternetOpenW(L"Mozilla/5.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return false;

    HINTERNET hConnect = InternetOpenUrlW(hInternet, L"https://www.bing.com", NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (hConnect) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return true;
    }
    InternetCloseHandle(hInternet);
    return false;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;

    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

// =====================================================
// File Management
// =====================================================

void nvCleanOldImages() {
    try {
        for (const auto& entry : fs::directory_iterator(INSTALL_DIR)) {
            if (entry.is_regular_file()) {
                std::wstring filename = entry.path().filename().wstring();

                // Only clean up historical files containing timestamps
                if (filename.rfind(L"wallpaper_", 0) == 0 && entry.path().extension() == L".jpg") {
                    if (filename == L"wallpaper.jpg" || filename == L"wallpaper_backup.jpg") {
                        continue;
                    }
                    if (filename.length() > 20) {
                        fs::remove(entry.path());
                    }
                }
            }
        }
    }
    catch (...) {}
}

// =====================================================
// Download & Verification
// =====================================================

// Added: Validates if the buffer contains a complete JPEG file structure
bool nvIsValidJpgBuffer(const std::string& buffer) {
    size_t len = buffer.size();
    if (len < 4) return false;

    // Check SOI (Start of Image): 0xFF, 0xD8
    if ((unsigned char)buffer[0] != 0xFF || (unsigned char)buffer[1] != 0xD8) {
        return false;
    }

    // Check EOI (End of Image): 0xFF, 0xD9
    // If network terminates early, these two bytes will not match EOI marker
    if ((unsigned char)buffer[len - 2] != 0xFF || (unsigned char)buffer[len - 1] != 0xD9) {
        return false;
    }

    return true;
}

bool nvDownloadImage() {
    const int max_retry = 10;

    for (int attempt = 1; attempt <= max_retry; ++attempt) {
        HINTERNET hInternet = InternetOpenW(
            L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120 Safari/537.36",
            INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0
        );

        if (!hInternet) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            continue;
        }

        std::wstring headers = L"Accept: image/webp,image/apng,image/*,*/*;q=0.8\r\nReferer: https://img.nsmc.org.cn/\r\n";

        HINTERNET hUrl = InternetOpenUrlW(hInternet, IMAGE_URL.c_str(), headers.c_str(), -1, INTERNET_FLAG_RELOAD, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            std::this_thread::sleep_for(std::chrono::seconds(60));
            continue;
        }

        std::string buffer;
        char chunk[4096];
        DWORD bytesRead = 0;
        while (InternetReadFile(hUrl, chunk, sizeof(chunk), &bytesRead) && bytesRead > 0) {
            buffer.append(chunk, bytesRead);
        }

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        // Modified: Use strict SOI + EOI validation to ensure network integrity
        if (!nvIsValidJpgBuffer(buffer)) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            continue; // Integrity check failed, retry downloading
        }

        try {
            fs::create_directories(INSTALL_DIR);

            FILE* f1 = nullptr;
            FILE* f2 = nullptr;
            _wfopen_s(&f1, IMAGE_FILE.c_str(), L"wb");
            _wfopen_s(&f2, BACKUP_IMAGE_FILE.c_str(), L"wb");

            if (f1) { fwrite(buffer.data(), 1, buffer.size(), f1); fclose(f1); }
            if (f2) { fwrite(buffer.data(), 1, buffer.size(), f2); fclose(f2); }

            return true;
        }
        catch (...) {}
    }
    return false;
}

// =====================================================
// Image Processing
// =====================================================

bool nvProcessImage() {
    std::wstring image_path = IMAGE_FILE;

    if (!fs::exists(image_path)) {
        image_path = BACKUP_IMAGE_FILE;
    }
    if (!fs::exists(image_path)) {
        return false;
    }

    Bitmap* original = Bitmap::FromFile(image_path.c_str());
    if (!original || original->GetLastStatus() != Ok) {
        delete original;
        return false;
    }

    int orig_width = original->GetWidth();
    int orig_height = original->GetHeight();

    // 1. Draw Black Mask to cover watermark
    Graphics* orig_graphics = Graphics::FromImage(original);
    SolidBrush blackBrush(Color(255, 0, 0, 0));
    orig_graphics->FillRectangle(&blackBrush, 0, 0, 5000, 1500);
    delete orig_graphics;

    // 2. Calculate Crop Box
    int screen_width, screen_height;
    nvGetScreenResolution(screen_width, screen_height);

    double target_ratio = (double)screen_width / screen_height;
    double current_ratio = (double)orig_width / orig_height;

    int src_x = 0, src_y = 0, src_w = orig_width, src_h = orig_height;

    if (current_ratio > target_ratio) {
        src_w = static_cast<int>(orig_height * target_ratio);
        src_x = (orig_width - src_w) / 2;
        src_y = 0;
    }
    else {
        src_w = orig_width;
        src_h = static_cast<int>(orig_width / target_ratio);
        src_x = 0;
        src_y = 0;
    }

    // 3. Create target canvas and Draw (Crop + Resize)
    Bitmap* target = new Bitmap(screen_width, screen_height, (PixelFormat)PixelFormat24bppRGB);
    Graphics* g = Graphics::FromImage(target);

    g->SetInterpolationMode(InterpolationModeBilinear);
    g->SetPixelOffsetMode(PixelOffsetModeNone);

    g->DrawImage(original,
        Rect(0, 0, screen_width, screen_height),
        src_x, src_y, src_w, src_h,
        UnitPixel
    );

    delete g;
    delete original;

    // 4. Prepare for Saving
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm time_info;
    localtime_s(&time_info, &in_time_t);
    ss << std::put_time(&time_info, "%Y%m%d_%H%M%S");

    std::wstring timestamp;
    std::string ts_str = ss.str();
    timestamp.assign(ts_str.begin(), ts_str.end());

    std::wstring debug_file = INSTALL_DIR + L"\\wallpaper_" + timestamp + L".jpg";

    CLSID clsid;
    GetEncoderClsid(L"image/jpeg", &clsid);

    EncoderParameters encoderParameters;
    encoderParameters.Count = 1;
    ULONG quality = 95;
    encoderParameters.Parameter[0].Guid = EncoderQuality;
    encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParameters.Parameter[0].NumberOfValues = 1;
    encoderParameters.Parameter[0].Value = &quality;

    target->Save(debug_file.c_str(), &clsid, &encoderParameters);
    target->Save(IMAGE_FILE.c_str(), &clsid, &encoderParameters);

    delete target;
    nvCleanOldImages();
    return true;
}

// =====================================================
// Wallpaper
// =====================================================

bool nvSetWallpaper() {
    return SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)IMAGE_FILE.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

// =====================================================
// Update Cycle
// =====================================================

void nvUpdateWallpaper() {
    if (!nvCheckNetwork()) return;

    if (nvDownloadImage()) {
        if (nvProcessImage()) {
            nvSetWallpaper();
        }
    }
    else {
        if (fs::exists(BACKUP_IMAGE_FILE)) {
            try {
                fs::copy_file(BACKUP_IMAGE_FILE, IMAGE_FILE, fs::copy_options::overwrite_existing);
                nvSetWallpaper();
            }
            catch (...) {}
        }
    }
}

void nvUpdateLoop() {
    nvUpdateWallpaper();
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(UPDATE_INTERVAL_SEC));
        nvUpdateWallpaper();
    }
}

// =====================================================
// Main Entry
// =====================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    wchar_t* userProfile = nullptr;
    size_t len = 0;
    _wdupenv_s(&userProfile, &len, L"USERPROFILE");
    if (userProfile) {
        INSTALL_DIR = std::wstring(userProfile) + L"\\Pictures\\" + APP_NAME;
        free(userProfile);
    }
    else {
        INSTALL_DIR = L"C:\\Pictures\\" + APP_NAME;
    }
    IMAGE_FILE = INSTALL_DIR + L"\\wallpaper.jpg";
    BACKUP_IMAGE_FILE = INSTALL_DIR + L"\\wallpaper_backup.jpg";

    try {
        fs::create_directories(INSTALL_DIR);
    }
    catch (...) {}

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    std::thread updaterThread(nvUpdateLoop);
    updaterThread.detach();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}