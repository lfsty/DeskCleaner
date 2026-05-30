#include "desktopIconManager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include <exdisp.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shtypes.h>
#include <strsafe.h>
#include <wrl/client.h>

namespace
{
using Microsoft::WRL::ComPtr;

constexpr DWORD kDesktopAlignmentFlagsMask = FWF_AUTOARRANGE | FWF_SNAPTOGRID;

struct ScopedComInitializer
{
    ScopedComInitializer()
        : result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
    {
    }

    ~ScopedComInitializer()
    {
        if (SUCCEEDED(result))
        {
            CoUninitialize();
        }
    }

    bool isUsable() const
    {
        return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
    }

    HRESULT result;
};

struct MonitorInfoSnapshot
{
    HMONITOR monitorHandle = nullptr;
    RECT monitorRect{};
    RECT workRect{};
    std::wstring deviceName;
};

BOOL CALLBACK CollectMonitorInfo(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    auto* monitors = reinterpret_cast<std::vector<MonitorInfoSnapshot>*>(data);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
    {
        return TRUE;
    }

    MonitorInfoSnapshot snapshot;
    snapshot.monitorHandle = monitor;
    snapshot.monitorRect = info.rcMonitor;
    snapshot.workRect = info.rcWork;
    snapshot.deviceName = info.szDevice;
    monitors->push_back(snapshot);

    return TRUE;
}

std::vector<MonitorInfoSnapshot> snapshotMonitors()
{
    std::vector<MonitorInfoSnapshot> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitorInfo, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

const MonitorInfoSnapshot* findMonitorByDeviceName(const std::vector<MonitorInfoSnapshot>& monitors,
                                                   const std::wstring& deviceName)
{
    const auto iterator = std::find_if(monitors.begin(), monitors.end(),
                                       [&deviceName](const MonitorInfoSnapshot& monitor)
                                       {
                                           return monitor.deviceName == deviceName;
                                       });

    if (iterator == monitors.end())
    {
        return nullptr;
    }

    return &(*iterator);
}

const MonitorInfoSnapshot* findMonitorByHandle(const std::vector<MonitorInfoSnapshot>& monitors,
                                               HMONITOR monitorHandle)
{
    const auto iterator = std::find_if(monitors.begin(), monitors.end(),
                                       [monitorHandle](const MonitorInfoSnapshot& monitor)
                                       {
                                           return monitor.monitorHandle == monitorHandle;
                                       });

    if (iterator == monitors.end())
    {
        return nullptr;
    }

    return &(*iterator);
}

const MonitorInfoSnapshot* findBestMonitorForPoint(const std::vector<MonitorInfoSnapshot>& monitors,
                                                   const POINT& point)
{
    for (const MonitorInfoSnapshot& monitor : monitors)
    {
        if (PtInRect(&monitor.workRect, point) || PtInRect(&monitor.monitorRect, point))
        {
            return &monitor;
        }
    }

    const HMONITOR nearestMonitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    return findMonitorByHandle(monitors, nearestMonitor);
}

HWND findDesktopListViewWindow()
{
    HWND shellView = FindWindowExW(FindWindowW(L"Progman", nullptr), nullptr, L"SHELLDLL_DefView", nullptr);

    if (!shellView)
    {
        HWND workerWindow = nullptr;
        while ((workerWindow = FindWindowExW(nullptr, workerWindow, L"WorkerW", nullptr)) != nullptr)
        {
            shellView = FindWindowExW(workerWindow, nullptr, L"SHELLDLL_DefView", nullptr);
            if (shellView)
            {
                break;
            }
        }
    }

    if (!shellView)
    {
        return nullptr;
    }

    return FindWindowExW(shellView, nullptr, WC_LISTVIEWW, L"FolderView");
}

bool getDesktopListViewOrigin(POINT& origin)
{
    HWND listViewWindow = findDesktopListViewWindow();
    if (!listViewWindow)
    {
        return false;
    }

    origin = POINT{0, 0};
    return ClientToScreen(listViewWindow, &origin) == TRUE;
}

HRESULT findDesktopFolderView(REFIID interfaceId, void** folderView)
{
    *folderView = nullptr;

    ComPtr<IShellWindows> shellWindows;
    HRESULT result = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&shellWindows));
    if (FAILED(result))
    {
        return result;
    }

    VARIANT location{};
    location.vt = VT_I4;
    location.lVal = CSIDL_DESKTOP;

    VARIANT empty{};
    long desktopWindowHandle = 0;
    ComPtr<IDispatch> dispatch;
    result = shellWindows->FindWindowSW(&location, &empty, SWC_DESKTOP,
                                        &desktopWindowHandle, SWFO_NEEDDISPATCH, &dispatch);
    if (FAILED(result) || !dispatch)
    {
        return FAILED(result) ? result : E_FAIL;
    }

    ComPtr<IServiceProvider> serviceProvider;
    result = dispatch.As(&serviceProvider);
    if (FAILED(result))
    {
        return result;
    }

    ComPtr<IShellBrowser> shellBrowser;
    result = serviceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&shellBrowser));
    if (FAILED(result))
    {
        return result;
    }

    ComPtr<IShellView> shellView;
    result = shellBrowser->QueryActiveShellView(&shellView);
    if (FAILED(result))
    {
        return result;
    }

    return shellView->QueryInterface(interfaceId, folderView);
}

std::wstring getDesktopItemKey(IShellFolder* shellFolder, PCUITEMID_CHILD itemId)
{
    STRRET displayName{};
    wchar_t buffer[1024] = {};

    if (SUCCEEDED(shellFolder->GetDisplayNameOf(itemId, SHGDN_FORPARSING, &displayName)) &&
        SUCCEEDED(StrRetToBufW(&displayName, itemId, buffer, ARRAYSIZE(buffer))))
    {
        return buffer;
    }

    if (SUCCEEDED(shellFolder->GetDisplayNameOf(itemId, SHGDN_INFOLDER, &displayName)) &&
        SUCCEEDED(StrRetToBufW(&displayName, itemId, buffer, ARRAYSIZE(buffer))))
    {
        return buffer;
    }

    return {};
}

POINT clampPointToWorkArea(const POINT& point, const RECT& workArea)
{
    POINT clamped = point;
    const int left = static_cast<int>(workArea.left);
    const int right = static_cast<int>(workArea.right) - 1;
    const int top = static_cast<int>(workArea.top);
    const int bottom = static_cast<int>(workArea.bottom) - 1;
    clamped.x = std::clamp(static_cast<int>(clamped.x), left, right);
    clamped.y = std::clamp(static_cast<int>(clamped.y), top, bottom);
    return clamped;
}

int workAreaWidth(const RECT& workArea)
{
    return (std::max)(1, static_cast<int>(workArea.right - workArea.left));
}

int workAreaHeight(const RECT& workArea)
{
    return (std::max)(1, static_cast<int>(workArea.bottom - workArea.top));
}

POINT buildRestoredScreenPoint(const std::wstring& monitorDeviceName,
                               int absoluteScreenX,
                               int absoluteScreenY,
                               int relativeToMonitorWorkAreaX,
                               int relativeToMonitorWorkAreaY,
                               int savedMonitorWorkWidth,
                               int savedMonitorWorkHeight,
                               const std::vector<MonitorInfoSnapshot>& monitors)
{
    const MonitorInfoSnapshot* targetMonitor = findMonitorByDeviceName(monitors, monitorDeviceName);

    if (targetMonitor && savedMonitorWorkWidth > 0 && savedMonitorWorkHeight > 0)
    {
        const int targetWidth = workAreaWidth(targetMonitor->workRect);
        const int targetHeight = workAreaHeight(targetMonitor->workRect);

        POINT scaledPoint{};
        scaledPoint.x = targetMonitor->workRect.left +
                        static_cast<int>(std::lround(static_cast<double>(relativeToMonitorWorkAreaX) *
                                                     static_cast<double>(targetWidth) /
                                                     static_cast<double>(savedMonitorWorkWidth)));
        scaledPoint.y = targetMonitor->workRect.top +
                        static_cast<int>(std::lround(static_cast<double>(relativeToMonitorWorkAreaY) *
                                                     static_cast<double>(targetHeight) /
                                                     static_cast<double>(savedMonitorWorkHeight)));
        return clampPointToWorkArea(scaledPoint, targetMonitor->workRect);
    }

    POINT absolutePoint{absoluteScreenX, absoluteScreenY};
    if (const MonitorInfoSnapshot* monitor = findBestMonitorForPoint(monitors, absolutePoint))
    {
        return clampPointToWorkArea(absolutePoint, monitor->workRect);
    }

    return absolutePoint;
}
} // namespace

DesktopIconManager* DesktopIconManager::getInstance()
{
    static DesktopIconManager instance;
    return &instance;
}

void DesktopIconManager::disableDesktopIconAlignment()
{
    ScopedComInitializer comInitializer;
    if (!comInitializer.isUsable())
    {
        return;
    }

    ComPtr<IFolderView2> folderView;
    if (FAILED(findDesktopFolderView(IID_PPV_ARGS(&folderView))))
    {
        return;
    }

    DWORD currentFlags = 0;
    if (FAILED(folderView->GetCurrentFolderFlags(&currentFlags)))
    {
        return;
    }

    if (!m_hasSavedAlignmentFlags)
    {
        m_savedAlignmentFlags = static_cast<std::uint32_t>(currentFlags & kDesktopAlignmentFlagsMask);
        m_hasSavedAlignmentFlags = true;
    }

    folderView->SetCurrentFolderFlags(kDesktopAlignmentFlagsMask, 0);
}

void DesktopIconManager::restoreDesktopIconAlignment()
{
    if (!m_hasSavedAlignmentFlags)
    {
        return;
    }

    ScopedComInitializer comInitializer;
    if (!comInitializer.isUsable())
    {
        return;
    }

    ComPtr<IFolderView2> folderView;
    if (FAILED(findDesktopFolderView(IID_PPV_ARGS(&folderView))))
    {
        return;
    }

    if (FAILED(folderView->SetCurrentFolderFlags(kDesktopAlignmentFlagsMask,
                                                 static_cast<DWORD>(m_savedAlignmentFlags))))
    {
        return;
    }

    m_savedAlignmentFlags = 0;
    m_hasSavedAlignmentFlags = false;
}

void DesktopIconManager::storeDesktopIcons()
{
    m_savedIcons.clear();

    ScopedComInitializer comInitializer;
    if (!comInitializer.isUsable())
    {
        return;
    }

    ComPtr<IFolderView> folderView;
    if (FAILED(findDesktopFolderView(IID_PPV_ARGS(&folderView))))
    {
        return;
    }

    ComPtr<IShellFolder> shellFolder;
    if (FAILED(folderView->GetFolder(IID_PPV_ARGS(&shellFolder))))
    {
        return;
    }

    int itemCount = 0;
    if (FAILED(folderView->ItemCount(SVGIO_ALLVIEW, &itemCount)))
    {
        return;
    }

    POINT desktopOrigin{};
    const bool hasDesktopOrigin = getDesktopListViewOrigin(desktopOrigin);
    const std::vector<MonitorInfoSnapshot> monitors = snapshotMonitors();

    for (int index = 0; index < itemCount; ++index)
    {
        PIDLIST_RELATIVE itemId = nullptr;
        if (FAILED(folderView->Item(index, &itemId)) || !itemId)
        {
            continue;
        }

        const std::wstring itemKey = getDesktopItemKey(shellFolder.Get(), itemId);
        if (itemKey.empty())
        {
            CoTaskMemFree(itemId);
            continue;
        }

        POINT itemPoint{};
        if (FAILED(folderView->GetItemPosition(itemId, &itemPoint)))
        {
            CoTaskMemFree(itemId);
            continue;
        }

        POINT screenPoint = itemPoint;
        if (hasDesktopOrigin)
        {
            screenPoint.x += desktopOrigin.x;
            screenPoint.y += desktopOrigin.y;
        }

        StoredDesktopIcon savedIcon;
        savedIcon.itemKey = itemKey;
        savedIcon.absoluteScreenX = screenPoint.x;
        savedIcon.absoluteScreenY = screenPoint.y;

        if (const MonitorInfoSnapshot* monitor = findBestMonitorForPoint(monitors, screenPoint))
        {
            savedIcon.monitorDeviceName = monitor->deviceName;
            savedIcon.relativeToMonitorWorkAreaX = screenPoint.x - monitor->workRect.left;
            savedIcon.relativeToMonitorWorkAreaY = screenPoint.y - monitor->workRect.top;
            savedIcon.savedMonitorWorkWidth = workAreaWidth(monitor->workRect);
            savedIcon.savedMonitorWorkHeight = workAreaHeight(monitor->workRect);
        }

        m_savedIcons[itemKey] = savedIcon;
        CoTaskMemFree(itemId);
    }
}

void DesktopIconManager::restoreDesktopIcons()
{
    auto restoreAlignmentState = [this]()
    {
        restoreDesktopIconAlignment();
    };

    if (m_savedIcons.empty())
    {
        restoreAlignmentState();
        return;
    }

    ScopedComInitializer comInitializer;
    if (!comInitializer.isUsable())
    {
        restoreAlignmentState();
        return;
    }

    ComPtr<IFolderView> folderView;
    if (FAILED(findDesktopFolderView(IID_PPV_ARGS(&folderView))))
    {
        restoreAlignmentState();
        return;
    }

    ComPtr<IShellFolder> shellFolder;
    if (FAILED(folderView->GetFolder(IID_PPV_ARGS(&shellFolder))))
    {
        restoreAlignmentState();
        return;
    }

    int itemCount = 0;
    if (FAILED(folderView->ItemCount(SVGIO_ALLVIEW, &itemCount)))
    {
        restoreAlignmentState();
        return;
    }

    POINT desktopOrigin{};
    const bool hasDesktopOrigin = getDesktopListViewOrigin(desktopOrigin);
    const std::vector<MonitorInfoSnapshot> monitors = snapshotMonitors();

    std::vector<PIDLIST_RELATIVE> ownedItemIds;
    std::vector<PCUITEMID_CHILD> itemIds;
    std::vector<POINT> targetPoints;
    ownedItemIds.reserve(itemCount);
    itemIds.reserve(itemCount);
    targetPoints.reserve(itemCount);

    for (int index = 0; index < itemCount; ++index)
    {
        PIDLIST_RELATIVE itemId = nullptr;
        if (FAILED(folderView->Item(index, &itemId)) || !itemId)
        {
            continue;
        }

        const std::wstring itemKey = getDesktopItemKey(shellFolder.Get(), itemId);
        const auto savedIconIterator = m_savedIcons.find(itemKey);
        if (savedIconIterator == m_savedIcons.end())
        {
            CoTaskMemFree(itemId);
            continue;
        }

        POINT screenPoint = buildRestoredScreenPoint(savedIconIterator->second.monitorDeviceName,
                                                     savedIconIterator->second.absoluteScreenX,
                                                     savedIconIterator->second.absoluteScreenY,
                                                     savedIconIterator->second.relativeToMonitorWorkAreaX,
                                                     savedIconIterator->second.relativeToMonitorWorkAreaY,
                                                     savedIconIterator->second.savedMonitorWorkWidth,
                                                     savedIconIterator->second.savedMonitorWorkHeight,
                                                     monitors);
        POINT clientPoint = screenPoint;
        if (hasDesktopOrigin)
        {
            clientPoint.x -= desktopOrigin.x;
            clientPoint.y -= desktopOrigin.y;
        }

        ownedItemIds.push_back(itemId);
        itemIds.push_back(itemId);
        targetPoints.push_back(clientPoint);
    }

    if (!itemIds.empty())
    {
        folderView->SelectAndPositionItems(static_cast<UINT>(itemIds.size()), itemIds.data(),
                                           targetPoints.data(), SVSI_POSITIONITEM);
    }

    for (PIDLIST_RELATIVE itemId : ownedItemIds)
    {
        CoTaskMemFree(itemId);
    }

    restoreAlignmentState();
}

void DesktopIconManager::moveDesktopIconsTo(const int x, const int y, const int width, const int height)
{
    disableDesktopIconAlignment();

    ScopedComInitializer comInitializer;
    if (!comInitializer.isUsable())
    {
        return;
    }

    ComPtr<IFolderView> folderView;
    if (FAILED(findDesktopFolderView(IID_PPV_ARGS(&folderView))))
    {
        return;
    }

    int itemCount = 0;
    if (FAILED(folderView->ItemCount(SVGIO_ALLVIEW, &itemCount)) || itemCount <= 0)
    {
        return;
    }

    POINT desktopOrigin{};
    const bool hasDesktopOrigin = getDesktopListViewOrigin(desktopOrigin);
    const std::vector<MonitorInfoSnapshot> monitors = snapshotMonitors();

    const int effectiveWidth = (std::max)(1, width);
    const int effectiveHeight = (std::max)(1, height);

    std::random_device randomDevice;
    std::mt19937 randomGenerator(randomDevice());
    std::uniform_int_distribution<int> xDistribution(x, x + effectiveWidth - 1);
    std::uniform_int_distribution<int> yDistribution(y, y + effectiveHeight - 1);

    std::vector<PIDLIST_RELATIVE> ownedItemIds;
    std::vector<PCUITEMID_CHILD> itemIds;
    std::vector<POINT> targetPoints;
    ownedItemIds.reserve(itemCount);
    itemIds.reserve(itemCount);
    targetPoints.reserve(itemCount);

    for (int index = 0; index < itemCount; ++index)
    {
        PIDLIST_RELATIVE itemId = nullptr;
        if (FAILED(folderView->Item(index, &itemId)) || !itemId)
        {
            continue;
        }

        POINT screenPoint{xDistribution(randomGenerator), yDistribution(randomGenerator)};
        if (const MonitorInfoSnapshot* monitor = findBestMonitorForPoint(monitors, screenPoint))
        {
            screenPoint = clampPointToWorkArea(screenPoint, monitor->workRect);
        }

        POINT clientPoint = screenPoint;
        if (hasDesktopOrigin)
        {
            clientPoint.x -= desktopOrigin.x;
            clientPoint.y -= desktopOrigin.y;
        }

        ownedItemIds.push_back(itemId);
        itemIds.push_back(itemId);
        targetPoints.push_back(clientPoint);
    }

    if (!itemIds.empty())
    {
        folderView->SelectAndPositionItems(static_cast<UINT>(itemIds.size()), itemIds.data(),
                                           targetPoints.data(), SVSI_POSITIONITEM);
    }

    for (PIDLIST_RELATIVE itemId : ownedItemIds)
    {
        CoTaskMemFree(itemId);
    }
}
