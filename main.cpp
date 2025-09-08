#define UNICODE
#define _UNICODE
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <initguid.h>
#include <devguid.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <sstream>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// Define GUIDs (remove duplicate GUID_DEVCLASS_MEDIA)
DEFINE_GUID(GUID_DEVCLASS_AUDIO, 0x4d36e96c, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);

// Window controls IDs
#define ID_BTN_DETECT 1001
#define ID_BTN_REFRESH 1002
#define ID_BTN_DISABLE 1003
#define ID_BTN_ENABLE 1004
#define ID_LIST_DEVICES 1005
#define ID_STATIC_STATUS 1006
#define ID_TIMER_DETECT 1007
#define ID_CHECK_AUTODETECT 1008

// Global variables
HWND g_hWnd = NULL;
HWND g_hListBox = NULL;
HWND g_hStatusText = NULL;
HWND g_hAutoDetectCheck = NULL;
bool g_AutoDetect = false;

struct AudioDevice {
    std::wstring name;
    std::wstring instanceId;
    std::wstring status;
    bool isRealtek;
    bool isConnected;
};

class AudioDeviceManager {
public:
    static std::vector<AudioDevice> EnumerateAudioDevices() {
        std::vector<AudioDevice> devices;
        HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&GUID_DEVCLASS_AUDIO, NULL, NULL, DIGCF_PRESENT);
        
        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            return devices;
        }

        SP_DEVINFO_DATA deviceInfoData = {sizeof(SP_DEVINFO_DATA)};
        
        for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
            AudioDevice device;
            
            // Get device description
            WCHAR description[256] = {0};
            DWORD dataType, size = sizeof(description);
            if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData,
                                               SPDRP_DEVICEDESC, &dataType,
                                               (PBYTE)description, size, &size)) {
                device.name = description;
            }
            
            // Get instance ID
            WCHAR instanceId[256] = {0};
            if (SetupDiGetDeviceInstanceIdW(deviceInfoSet, &deviceInfoData,
                                         instanceId, sizeof(instanceId)/sizeof(WCHAR), NULL)) {
                device.instanceId = instanceId;
            }
            
            // Check if it's Realtek
            device.isRealtek = (device.name.find(L"Realtek") != std::wstring::npos) ||
                              (device.instanceId.find(L"REALTEK") != std::wstring::npos);
            
            // Get device status
            ULONG status = 0, problem = 0;
            if (CM_Get_DevNode_Status(&status, &problem, deviceInfoData.DevInst, 0) == CR_SUCCESS) {
                if (status & DN_STARTED) {
                    device.status = L"Active";
                    device.isConnected = true;
                } else if (problem) {
                    device.status = L"Problem";
                    device.isConnected = false;
                } else {
                    device.status = L"Disabled";
                    device.isConnected = false;
                }
            }
            
            devices.push_back(device);
        }
        
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return devices;
    }

    static bool RefreshDevice(const std::wstring& instanceId) {
        // First disable, then enable the device
        bool disabled = SetDeviceState(instanceId, false);
        if (disabled) {
            Sleep(500); // Wait a bit
            return SetDeviceState(instanceId, true);
        }
        return false;
    }

    static bool SetDeviceState(const std::wstring& instanceId, bool enable) {
        HDEVINFO deviceInfoSet = SetupDiGetClassDevs(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            return false;
        }

        SP_DEVINFO_DATA deviceInfoData = {sizeof(SP_DEVINFO_DATA)};
        bool found = false;

        for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
            WCHAR currentInstanceId[256] = {0};
            if (SetupDiGetDeviceInstanceIdW(deviceInfoSet, &deviceInfoData,
                                         currentInstanceId, sizeof(currentInstanceId)/sizeof(WCHAR), NULL)) {
                if (instanceId == currentInstanceId) {
                    found = true;
                    
                    SP_PROPCHANGE_PARAMS propChangeParams = {0};
                    propChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
                    propChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
                    propChangeParams.StateChange = enable ? DICS_ENABLE : DICS_DISABLE;
                    propChangeParams.Scope = DICS_FLAG_GLOBAL;
                    propChangeParams.HwProfile = 0;

                    if (SetupDiSetClassInstallParams(deviceInfoSet, &deviceInfoData,
                                                   (PSP_CLASSINSTALL_HEADER)&propChangeParams,
                                                   sizeof(propChangeParams))) {
                        SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, deviceInfoSet, &deviceInfoData);
                    }
                    break;
                }
            }
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return found;
    }

    static std::wstring DetectHeadphones() {
        CoInitialize(NULL);
        
        IMMDeviceEnumerator* pEnumerator = NULL;
        IMMDeviceCollection* pCollection = NULL;
        std::wstring result = L"No headphones detected";
        
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            NULL,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&pEnumerator
        );
        
        if (SUCCEEDED(hr)) {
            hr = pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
            
            if (SUCCEEDED(hr)) {
                UINT count;
                pCollection->GetCount(&count);
                
                for (UINT i = 0; i < count; i++) {
                    IMMDevice* pDevice = NULL;
                    hr = pCollection->Item(i, &pDevice);
                    
                    if (SUCCEEDED(hr)) {
                        IPropertyStore* pProps = NULL;
                        hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
                        
                        if (SUCCEEDED(hr)) {
                            PROPVARIANT varName;
                            PropVariantInit(&varName);
                            
                            hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
                            if (SUCCEEDED(hr)) {
                                std::wstring deviceName = varName.pwszVal;
                                
                                // Check for common headphone/headset indicators
                                if (deviceName.find(L"Headphone") != std::wstring::npos ||
                                    deviceName.find(L"Headset") != std::wstring::npos ||
                                    deviceName.find(L"Earphone") != std::wstring::npos) {
                                    result = L"Headphones detected: " + deviceName;
                                    PropVariantClear(&varName);
                                    pProps->Release();
                                    pDevice->Release();
                                    break;
                                }
                            }
                            
                            PropVariantClear(&varName);
                            pProps->Release();
                        }
                        pDevice->Release();
                    }
                }
                pCollection->Release();
            }
            pEnumerator->Release();
        }
        
        CoUninitialize();
        return result;
    }
};

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        {
            // Create controls
            CreateWindowW(L"BUTTON", L"Detect Headphones",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                10, 10, 150, 30,
                hwnd, (HMENU)ID_BTN_DETECT, NULL, NULL);
            
            CreateWindowW(L"BUTTON", L"Refresh Realtek",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                170, 10, 150, 30,
                hwnd, (HMENU)ID_BTN_REFRESH, NULL, NULL);
            
            CreateWindowW(L"BUTTON", L"Disable Selected",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                330, 10, 120, 30,
                hwnd, (HMENU)ID_BTN_DISABLE, NULL, NULL);
            
            CreateWindowW(L"BUTTON", L"Enable Selected",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                460, 10, 120, 30,
                hwnd, (HMENU)ID_BTN_ENABLE, NULL, NULL);
            
            g_hAutoDetectCheck = CreateWindowW(L"BUTTON", L"Auto-detect",
                WS_VISIBLE | WS_CHILD | BS_CHECKBOX,
                590, 10, 100, 30,
                hwnd, (HMENU)ID_CHECK_AUTODETECT, NULL, NULL);
            
            // Create status text
            g_hStatusText = CreateWindowW(L"STATIC", L"Status: Ready",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                10, 50, 680, 30,
                hwnd, (HMENU)ID_STATIC_STATUS, NULL, NULL);
            
            // Create list box for devices
            g_hListBox = CreateWindowW(L"LISTBOX", NULL,
                WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                10, 90, 680, 350,
                hwnd, (HMENU)ID_LIST_DEVICES, NULL, NULL);
            
            // Set font
            HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            
            SendMessage(g_hStatusText, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hListBox, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Initial device enumeration
            PostMessage(hwnd, WM_COMMAND, ID_BTN_DETECT, 0);
        }
        return 0;
        
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            switch (wmId) {
            case ID_BTN_DETECT:
                {
                    // Clear list
                    SendMessage(g_hListBox, LB_RESETCONTENT, 0, 0);
                    
                    // Detect headphones
                    std::wstring headphoneStatus = AudioDeviceManager::DetectHeadphones();
                    SetWindowTextW(g_hStatusText, (L"Status: " + headphoneStatus).c_str());
                    
                    // Enumerate devices
                    auto devices = AudioDeviceManager::EnumerateAudioDevices();
                    for (const auto& device : devices) {
                        std::wstring displayText = device.name;
                        if (device.isRealtek) displayText += L" [REALTEK]";
                        displayText += L" - " + device.status;
                        
                        int index = (int)SendMessageW(g_hListBox, LB_ADDSTRING, 0, (LPARAM)displayText.c_str());
                        SendMessage(g_hListBox, LB_SETITEMDATA, index, (LPARAM)new std::wstring(device.instanceId));
                    }
                }
                break;
                
            case ID_BTN_REFRESH:
                {
                    SetWindowTextW(g_hStatusText, L"Status: Refreshing Realtek devices...");
                    
                    auto devices = AudioDeviceManager::EnumerateAudioDevices();
                    int refreshed = 0;
                    
                    for (const auto& device : devices) {
                        if (device.isRealtek) {
                            if (AudioDeviceManager::RefreshDevice(device.instanceId)) {
                                refreshed++;
                            }
                        }
                    }
                    
                    std::wstring status = L"Status: Refreshed " + std::to_wstring(refreshed) + L" Realtek device(s)";
                    SetWindowTextW(g_hStatusText, status.c_str());
                    
                    // Refresh the list
                    PostMessage(hwnd, WM_COMMAND, ID_BTN_DETECT, 0);
                }
                break;
                
            case ID_BTN_DISABLE:
            case ID_BTN_ENABLE:
                {
                    int selected = (int)SendMessage(g_hListBox, LB_GETCURSEL, 0, 0);
                    if (selected != LB_ERR) {
                        std::wstring* instanceId = (std::wstring*)SendMessage(g_hListBox, LB_GETITEMDATA, selected, 0);
                        if (instanceId) {
                            bool enable = (wmId == ID_BTN_ENABLE);
                            if (AudioDeviceManager::SetDeviceState(*instanceId, enable)) {
                                std::wstring status = enable ? L"Status: Device enabled" : L"Status: Device disabled";
                                SetWindowTextW(g_hStatusText, status.c_str());
                                
                                // Refresh the list
                                Sleep(500);
                                PostMessage(hwnd, WM_COMMAND, ID_BTN_DETECT, 0);
                            } else {
                                SetWindowTextW(g_hStatusText, L"Status: Failed to change device state");
                            }
                        }
                    } else {
                        SetWindowTextW(g_hStatusText, L"Status: Please select a device first");
                    }
                }
                break;
                
            case ID_CHECK_AUTODETECT:
                {
                    g_AutoDetect = (SendMessage(g_hAutoDetectCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    if (g_AutoDetect) {
                        SetTimer(hwnd, ID_TIMER_DETECT, 2000, NULL); // Check every 2 seconds
                        SetWindowTextW(g_hStatusText, L"Status: Auto-detection enabled");
                    } else {
                        KillTimer(hwnd, ID_TIMER_DETECT);
                        SetWindowTextW(g_hStatusText, L"Status: Auto-detection disabled");
                    }
                }
                break;
            }
        }
        return 0;
        
    case WM_TIMER:
        if (wParam == ID_TIMER_DETECT) {
            // Auto-detect headphones
            std::wstring headphoneStatus = AudioDeviceManager::DetectHeadphones();
            SetWindowTextW(g_hStatusText, (L"Status: " + headphoneStatus).c_str());
        }
        return 0;
        
    case WM_SIZE:
        {
            RECT rect;
            GetClientRect(hwnd, &rect);
            
            // Resize listbox to fit window
            SetWindowPos(g_hListBox, NULL, 10, 90, 
                rect.right - 20, rect.bottom - 100,
                SWP_NOZORDER);
        }
        return 0;
        
    case WM_DESTROY:
        // Clean up item data
        {
            int count = (int)SendMessage(g_hListBox, LB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++) {
                std::wstring* instanceId = (std::wstring*)SendMessage(g_hListBox, LB_GETITEMDATA, i, 0);
                if (instanceId) delete instanceId;
            }
        }
        PostQuitMessage(0);
        return 0;
        
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Draw background
            RECT rect;
            GetClientRect(hwnd, &rect);
            FillRect(hdc, &rect, (HBRUSH)(COLOR_WINDOW + 1));
            
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Main entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);
    
    // Register window class
    const wchar_t CLASS_NAME[] = L"AudioDeviceManager";
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Window Registration Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    // Create window
    g_hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Audio Device Manager - Headphone Detector & Realtek Refresher",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 500,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    
    if (g_hWnd == NULL) {
        MessageBoxW(NULL, L"Window Creation Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    
    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}
