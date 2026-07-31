#include <windows.h>
#include <gdiplus.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>
#include <combaseapi.h>
#include <commdlg.h>
#include <d3d11.h>
#include <wincrypt.h>
#include <wininet.h>

#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <iomanip>

// Автоматическая линковка системных библиотек
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "gdiplus.lib")

#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_win32.h"
#include "../imgui/imgui_impl_dx11.h"

// --- Глобальные переменные DirectX ---
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*      g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;
static ID3D11ShaderResourceView* g_appIconTexture = nullptr;

// Forward declarations DirectX
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- Структуры и переменные Инжектора ---
struct ProcessInfo {
    DWORD pid;
    std::string name;
    std::string path;
};

struct ScanResult {
    bool is_scanned = false;
    int positives = 0;
    int total = 0;
    std::string status_text = "Не проверено";
};

static char process_name_input[128] = "javaw.exe";
static char file_path_input[256] = "";
static char vt_api_key_input[128] = ""; 
static ScanResult current_scan;

static int current_method = 0;
static const char* injection_methods[] = { "Kernel Mode", "LoadLibrary", "Manual Map", "JNI Agent Attach (JAR)" };

// Состояние окон
static bool show_process_selector = false;
static bool splash_screen_active = true;
static float splash_timer = 0.0f;

static std::vector<ProcessInfo> process_list;
static char process_filter[128] = "";
static int selected_process_idx = -1;

// --- Вспомогательная структура для разворачивания окна процесса ---
struct EnumData {
    DWORD pid;
    HWND hWnd;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    EnumData& data = *(EnumData*)lParam;
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    if (processId == data.pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == NULL) {
        data.hWnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

// --- Функция переключения фокуса на программу после инжекта ---
bool BringProcessToForeground(const std::string& processName) {
    DWORD targetPid = 0;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, &pe32)) {
            do {
                if (_stricmp(pe32.szExeFile, processName.c_str()) == 0) {
                    targetPid = pe32.th32ProcessID;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe32));
        }
        CloseHandle(hSnapshot);
    }

    if (targetPid == 0) return false;

    EnumData data = { targetPid, NULL };
    EnumWindows(EnumWindowsProc, (LPARAM)&data);

    if (data.hWnd != NULL) {
        if (IsIconic(data.hWnd)) {
            ShowWindow(data.hWnd, SW_RESTORE);
        } else {
            ShowWindow(data.hWnd, SW_SHOW);
        }

        DWORD foregroundThreadId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
        DWORD currentThreadId = GetCurrentThreadId();

        AttachThreadInput(currentThreadId, foregroundThreadId, TRUE);
        SetForegroundWindow(data.hWnd);
        BringWindowToTop(data.hWnd);
        AttachThreadInput(currentThreadId, foregroundThreadId, FALSE);

        return true;
    }

    return false;
}

// --- Логика инжекта JAR (JNI Attach / JVMTI) ---
bool InjectJAR(const std::string& processName, const std::string& jarPath) {
    // В реальном проекте здесь используется загрузка jvm.dll и вызов
    // JNI_GetCreatedJavaVMs для выполнения метода агента внутри приложения Java.
    // Пример вызова через команду запуск-агента или кастомного DLL-лоадера.
    
    // Временная заглушка для демонстрации вызова
    if (jarPath.empty()) return false;
    
    // Например, можно вызвать запуск встроенного Java-агента:
    // std::string cmd = "java -jar injector.jar " + processName + " " + jarPath;
    // system(cmd.c_str());

    return true;
}

// --- Логика инжекта DLL ---
bool InjectDLL(const std::string& processName, const std::string& dllPath, int method) {
    if (dllPath.empty()) return false;
    // Обычная логика внедрения DLL (LoadLibrary/ManualMap/Kernel)
    return true;
}

// --- Загрузка иконки (icon.webp) через GDI+ в DX11 текстуру ---
bool LoadIconTexture(const wchar_t* filename, ID3D11Device* d3dDevice, ID3D11ShaderResourceView** out_srv) {
    Gdiplus::Bitmap bitmap(filename);
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return false;

    UINT width = bitmap.GetWidth();
    UINT height = bitmap.GetHeight();

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData bitmapData;
    bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData);

    std::vector<unsigned char> pixels(width * height * 4);
    unsigned char* src = (unsigned char*)bitmapData.Scan0;

    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            UINT i = (y * width + x) * 4;
            pixels[i + 0] = src[i + 2]; // Red
            pixels[i + 1] = src[i + 1]; // Green
            pixels[i + 2] = src[i + 0]; // Blue
            pixels[i + 3] = src[i + 3]; // Alpha
        }
        src += bitmapData.Stride;
    }
    bitmap.UnlockBits(&bitmapData);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subData = {};
    subData.pSysMem = pixels.data();
    subData.SysMemPitch = width * 4;

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = d3dDevice->CreateTexture2D(&desc, &subData, &pTexture);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = d3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
    pTexture->Release();

    return SUCCEEDED(hr);
}

// --- Диалоговое окно выбора DLL и JAR файлов ---
void OpenFileDialog(HWND hwnd) {
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    // Поддержка и DLL, и JAR файлов
    ofn.lpstrFilter = "Supported Files (*.dll;*.jar)\0*.dll;*.jar\0Dynamic Link Library (*.dll)\0*.dll\0Java Archive (*.jar)\0*.jar\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        strcpy_s(file_path_input, szFile);
    }
}

// --- Вычисление SHA-256 хеша ---
std::string GetFileSHA256(const std::string& filePath) {
    HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return "";

    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE buffer[8192];
    DWORD bytesRead = 0;
    BYTE hash[32];
    DWORD hashLen = 32;
    std::string hashStr = "";

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
                CryptHashData(hHash, buffer, bytesRead, 0);
            }
            if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
                std::stringstream ss;
                for (DWORD i = 0; i < hashLen; i++) {
                    ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
                }
                hashStr = ss.str();
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }
    CloseHandle(hFile);
    return hashStr;
}

// --- Запрос проверки на VirusTotal ---
ScanResult CheckVirusTotal(const std::string& sha256_hash, const std::string& apiKey) {
    ScanResult result;
    if (sha256_hash.empty()) {
        result.status_text = "Ошибка: Файл не найден!";
        return result;
    }
    if (apiKey.empty()) {
        result.status_text = "Ошибка: Введите API Ключ VirusTotal!";
        return result;
    }

    HINTERNET hInternet = InternetOpenA("SuperiorInjectorScanner", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return result;

    std::string url = "/api/v3/files/" + sha256_hash;
    HINTERNET hConnect = InternetConnectA(hInternet, "www.virustotal.com", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);

    if (hConnect) {
        HINTERNET hRequest = HttpOpenRequestA(hConnect, "GET", url.c_str(), NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
        if (hRequest) {
            std::string headers = "x-apikey: " + apiKey + "\r\n";
            HttpAddRequestHeadersA(hRequest, headers.c_str(), (DWORD)headers.length(), HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);

            if (HttpSendRequestA(hRequest, NULL, 0, NULL, 0)) {
                char buffer[4096];
                DWORD bytesRead = 0;
                std::string response = "";
                while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
                    buffer[bytesRead] = 0;
                    response += buffer;
                }

                if (response.find("\"malicious\":") != std::string::npos) {
                    size_t pos = response.find("\"malicious\":");
                    result.positives = std::stoi(response.substr(pos + 12, 4));
                    result.is_scanned = true;
                    result.status_text = "Обнаружено детектов: " + std::to_string(result.positives);
                } else if (response.find("NotFoundError") != std::string::npos) {
                    result.status_text = "Файл не найден в базе VirusTotal";
                    result.is_scanned = true;
                } else {
                    result.status_text = "Ошибка ответа API / Неверный ключ";
                }
            }
            InternetCloseHandle(hRequest);
        }
        InternetCloseHandle(hConnect);
    }
    InternetCloseHandle(hInternet);
    return result;
}

// --- Обновление списка активных процессов ---
void RefreshProcessList() {
    process_list.clear();
    selected_process_idx = -1;

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &pe32)) {
        do {
            ProcessInfo info;
            info.pid = pe32.th32ProcessID;
            info.name = pe32.szExeFile;

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, info.pid);
            if (hProcess) {
                char path[MAX_PATH];
                if (GetModuleFileNameExA(hProcess, NULL, path, MAX_PATH)) {
                    info.path = path;
                }
                CloseHandle(hProcess);
            }

            process_list.push_back(info);
        } while (Process32Next(hSnapshot, &pe32));
    }

    CloseHandle(hSnapshot);

    std::sort(process_list.begin(), process_list.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
        std::string nameA = a.name;
        std::string nameB = b.name;
        std::transform(nameA.begin(), nameA.end(), nameA.begin(), ::tolower);
        std::transform(nameB.begin(), nameB.end(), nameB.begin(), ::tolower);
        return nameA < nameB;
    });
}

// --- Главная точка входа WinMain ---
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Инициализация GDI+ для поддержки WebP/PNG
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"SuperiorInjectorClass", NULL };
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"SuperiorInjector v1.1.2", WS_OVERLAPPEDWINDOW, 100, 100, 520, 420, NULL, NULL, wc.hInstance, NULL);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Загрузка иконки приложения
    if (!LoadIconTexture(L"icon.webp", g_pd3dDevice, &g_appIconTexture)) {
        LoadIconTexture(L"SuperiorInjector/icon.webp", g_pd3dDevice, &g_appIconTexture);
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImFontConfig font_cfg;
    font_cfg.SizePixels = 16.0f;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // ----------------- 1. SPLASH SCREEN -----------------
        if (splash_screen_active) {
            splash_timer += io.DeltaTime;

            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("SplashScreen", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            float window_width = ImGui::GetWindowWidth();
            float window_height = ImGui::GetWindowHeight();

            if (g_appIconTexture) {
                ImGui::SetCursorPos(ImVec2((window_width - 48.0f) * 0.5f, window_height * 0.15f));
                ImGui::Image((void*)g_appIconTexture, ImVec2(48.0f, 48.0f));
            }

            std::string title_str = "SuperiorInjector v1.1.2";
            std::string author_str = "By Qwentozz";

            float title_width = ImGui::CalcTextSize(title_str.c_str()).x;
            float author_width = ImGui::CalcTextSize(author_str.c_str()).x;

            ImGui::SetCursorPos(ImVec2((window_width - title_width) * 0.5f, window_height * 0.32f));
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), title_str.c_str());

            ImGui::SetCursorPos(ImVec2((window_width - author_width) * 0.5f, window_height * 0.42f));
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), author_str.c_str());

            float progress = splash_timer / 3.0f;
            if (progress > 1.0f) progress = 1.0f;

            ImGui::SetCursorPos(ImVec2(50, window_height * 0.65f));
            ImGui::PushItemWidth(window_width - 100);
            ImGui::ProgressBar(progress, ImVec2(0.0f, 18.0f), "");
            ImGui::PopItemWidth();

            std::string status = "Инициализация подсистем...";
            if (splash_timer > 1.0f) status = "Загрузка модулей проверки...";
            if (splash_timer > 2.0f) status = "Подготовка интерфейса...";

            float status_width = ImGui::CalcTextSize(status.c_str()).x;
            ImGui::SetCursorPos(ImVec2((window_width - status_width) * 0.5f, window_height * 0.75f));
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), status.c_str());

            if (splash_timer >= 3.0f) {
                splash_screen_active = false;
            }

            ImGui::End();
        } 
        else {
            // ----------------- 2. ГЛАВНЫЙ ИНТЕРФЕЙС -----------------
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Main UI", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

            if (g_appIconTexture) {
                ImGui::Image((void*)g_appIconTexture, ImVec2(24.0f, 24.0f));
                ImGui::SameLine();
            }

            ImGui::Text("SuperiorInjector v1.1.2");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "By Qwentozz");
            ImGui::Separator();
            ImGui::Spacing();

            // Поле процесса
            ImGui::Text("Целевой процесс:");
            ImGui::PushItemWidth(330);
            ImGui::InputText("##ProcessName", process_name_input, IM_ARRAYSIZE(process_name_input));
            ImGui::PopItemWidth();
            
            ImGui::SameLine();
            if (ImGui::Button("Выбрать...")) {
                RefreshProcessList();
                show_process_selector = true;
            }

            ImGui::Spacing();

            // Поле выбора файла (.dll / .jar)
            ImGui::Text("Файл (DLL / JAR):");
            ImGui::PushItemWidth(330);
            ImGui::InputText("##FilePath", file_path_input, IM_ARRAYSIZE(file_path_input));
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::Button("Select File...")) {
                OpenFileDialog(hwnd);
            }

            ImGui::Spacing();

            // Метод инжекта
            ImGui::Text("Метод инжекта:");
            ImGui::PushItemWidth(-1);
            ImGui::Combo("##Method", &current_method, injection_methods, IM_ARRAYSIZE(injection_methods));
            ImGui::PopItemWidth();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // VirusTotal сканер
            ImGui::Text("VirusTotal API Key:");
            ImGui::PushItemWidth(330);
            ImGui::InputText("##VTKey", vt_api_key_input, IM_ARRAYSIZE(vt_api_key_input), ImGuiInputTextFlags_Password);
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::Button("Проверить Файл")) {
                std::string hash = GetFileSHA256(file_path_input);
                current_scan = CheckVirusTotal(hash, vt_api_key_input);
            }

            if (current_scan.is_scanned) {
                if (current_scan.positives > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[ ВНИМАНИЕ ] %s", current_scan.status_text.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[ ЧИСТО ] 0 детектов в базе VirusTotal");
                }
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Статус: %s", current_scan.status_text.c_str());
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // Кнопка Инжекта
            if (ImGui::Button("INJECT NOW", ImVec2(-1, 40))) {
                std::string path_str = file_path_input;
                std::string ext = "";
                
                size_t dot_pos = path_str.find_last_of(".");
                if (dot_pos != std::string::npos) {
                    ext = path_str.substr(dot_pos);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                }

                // Авто-определение типа файла
                if (ext == ".jar") {
                    InjectJAR(process_name_input, file_path_input);
                } else {
                    InjectDLL(process_name_input, file_path_input, current_method);
                }
                
                // Переключаем фокус на целевой процесс
                BringProcessToForeground(process_name_input);
            }

            ImGui::End();
        }

        // ----------------- 3. PROCESS SELECTOR -----------------
        if (show_process_selector) {
            ImGui::SetNextWindowSize(ImVec2(440, 320), ImGuiCond_FirstUseEver);
            ImGui::Begin("Process Selector", &show_process_selector, ImGuiWindowFlags_NoScrollbar);

            ImGui::Text("Поиск:");
            ImGui::SameLine();
            ImGui::PushItemWidth(210);
            ImGui::InputText("##Filter", process_filter, IM_ARRAYSIZE(process_filter));
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::Button("Обновить")) {
                RefreshProcessList();
            }

            ImGui::Separator();

            float table_height = ImGui::GetContentRegionAvail().y - 35.0f;
            if (ImGui::BeginTable("ProcessTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, table_height))) {
                ImGui::TableSetupColumn("Имя процесса", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableHeadersRow();

                std::string filter_str = process_filter;
                std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

                for (int i = 0; i < (int)process_list.size(); i++) {
                    std::string name_lower = process_list[i].name;
                    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

                    if (!filter_str.empty() && name_lower.find(filter_str) == std::string::npos)
                        continue;

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    ImGui::PushID(process_list[i].pid + i);

                    bool is_selected = (selected_process_idx == i);
                    if (ImGui::Selectable(process_list[i].name.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        selected_process_idx = i;
                    }

                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                        strcpy_s(process_name_input, process_list[i].name.c_str());
                        show_process_selector = false;
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%lu", process_list[i].pid);

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::Separator();

            if (ImGui::Button("Выбрать", ImVec2(100, 0))) {
                if (selected_process_idx >= 0 && selected_process_idx < (int)process_list.size()) {
                    strcpy_s(process_name_input, process_list[selected_process_idx].name.c_str());
                }
                show_process_selector = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Отмена", ImVec2(100, 0))) {
                show_process_selector = false;
            }

            ImGui::End();
        }

        // Рендеринг
        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.12f, 0.12f, 0.12f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

    if (g_appIconTexture) g_appIconTexture->Release();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CoUninitialize();

    return 0;
}

// --- DirectX 11 Инициализация ---
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}