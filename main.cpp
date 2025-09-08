#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <iostream>
#include <vector>
#include <string>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

struct DeviceInfo {
    std::wstring instanceId;
    std::wstring description;
    GUID classGuid;
};

class DeviceManager {
public:
    static std::vector<DeviceInfo> EnumerateDevices(const GUID& deviceClass) {
        std::vector<DeviceInfo> devices;
        HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&deviceClass, NULL, NULL, DIGCF_PRESENT);
        
        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            return devices;
        }

        SP_DEVINFO_DATA deviceInfoData = {sizeof(SP_DEVINFO_DATA)};
        
        for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
            DeviceInfo deviceInfo;
            
            // Get device description
            WCHAR description[256];
            DWORD dataType, size = sizeof(description);
            if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData,
                                               SPDRP_DEVICEDESC, &dataType,
                                               (PBYTE)description, size, &size)) {
                deviceInfo.description = description;
            }
            
            // Get instance ID
            WCHAR instanceId[256];
            if (SetupDiGetDeviceInstanceIdW(deviceInfoSet, &deviceInfoData,
                                         instanceId, sizeof(instanceId)/sizeof(WCHAR), NULL)) {
                deviceInfo.instanceId = instanceId;
            }
            
            deviceInfo.classGuid = deviceClass;
            devices.push_back(deviceInfo);
        }
        
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return devices;
    }

    static bool EnableDisableDevice(const std::wstring& instanceId, bool enable) {
        DEVINST devInst;
        CONFIGRET cr;

        cr = CM_Locate_DevNodeW(&devInst, const_cast<LPWSTR>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL);
        if (cr != CR_SUCCESS) {
            return false;
        }

        // Alternative approach using SetupAPI
        return EnableDisableDeviceSetupAPI(instanceId, enable);
    }

private:
    static bool EnableDisableDeviceSetupAPI(const std::wstring& instanceId, bool enable) {
        // Implementation similar to Method 1 but targeting specific instance
        HDEVINFO deviceInfoSet = SetupDiGetClassDevs(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            return false;
        }

        SP_DEVINFO_DATA deviceInfoData = {sizeof(SP_DEVINFO_DATA)};
        bool found = false;

        for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData) && !found; i++) {
            WCHAR currentInstanceId[256];
            if (SetupDiGetDeviceInstanceIdW(deviceInfoSet, &deviceInfoData,
                                         currentInstanceId, sizeof(currentInstanceId)/sizeof(WCHAR), NULL)) {
                if (instanceId == std::wstring(currentInstanceId)) {
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
                }
            }
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return found;
    }
};

// Usage example
int main() {
    // Enumerate audio devices
    std::vector<DeviceInfo> audioDevices = DeviceManager::EnumerateDevices(GUID_DEVCLASS_AUDIO);
    
    std::wcout << L"Audio Devices:" << std::endl;
    for (size_t i = 0; i < audioDevices.size(); i++) {
        const DeviceInfo& device = audioDevices[i];
        std::wcout << L"  " << device.description << L" (" << device.instanceId << L")" << std::endl;
        
        // Enable/disable Realtek audio devices
        if (device.description.find(L"Realtek") != std::wstring::npos) {
            std::wcout << L"  -> Found Realtek device, disabling..." << std::endl;
            DeviceManager::EnableDisableDevice(device.instanceId, false); // Disable
            Sleep(1000);
            DeviceManager::EnableDisableDevice(device.instanceId, true);  // Enable
        }
    }

    return 0;
}
