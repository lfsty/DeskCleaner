#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

class DesktopIconManager
{
public:
    static DesktopIconManager* getInstance();

private:
    DesktopIconManager() = default;
    ~DesktopIconManager() = default;

public:
    // 存储桌面图标位置
    void storeDesktopIcons();

    // 恢复桌面图标位置
    void restoreDesktopIcons();

    // 移动桌面所有图标到指定位置
    void moveDesktopIconsTo(const int x, const int y, const int width, const int height);

    // 关闭桌面图标自动排列和与网格对齐，并保存旧状态
    void disableDesktopIconAlignment();

    // 恢复桌面图标自动排列和与网格对齐的旧状态
    void restoreDesktopIconAlignment();

private:
    struct StoredDesktopIcon
    {
        std::wstring itemKey;
        std::wstring monitorDeviceName;
        int absoluteScreenX = 0;
        int absoluteScreenY = 0;
        int relativeToMonitorWorkAreaX = 0;
        int relativeToMonitorWorkAreaY = 0;
        int savedMonitorWorkWidth = 0;
        int savedMonitorWorkHeight = 0;
    };

    std::unordered_map<std::wstring, StoredDesktopIcon> m_savedIcons;
    std::uint32_t m_savedAlignmentFlags = 0;
    bool m_hasSavedAlignmentFlags = false;
};