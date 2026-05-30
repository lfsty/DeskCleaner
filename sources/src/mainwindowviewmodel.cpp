#include "mainwindowviewmodel.h"

#include "desktopIconManager.h"

MainWindowViewModel::MainWindowViewModel(QObject* parent) : QObject(parent)
{
    // 在程序启动时就记录桌面图标位置，放置用户忘记恢复
    DesktopIconManager::getInstance()->storeDesktopIcons();
}

MainWindowViewModel::~MainWindowViewModel()
{
    // 在程序退出时恢复桌面图标位置，放置用户忘记恢复
    DesktopIconManager::getInstance()->restoreDesktopIcons();
}

QString MainWindowViewModel::windowTitle() const
{
    return QStringLiteral("桌面清理大师");
}

void MainWindowViewModel::setCleaning(bool cleaning)
{
    if (m_isCleaning != cleaning)
    {
        m_isCleaning = cleaning;
        emit cleaningStatusChanged(m_isCleaning);
    }
}

void MainWindowViewModel::clearDesktop(const QRect& rect)
{
    setCleaning(true);
    windowMoved(rect);
}

void MainWindowViewModel::windowMoved(const QRect& rect)
{
    if (m_isCleaning)
    {
        DesktopIconManager::getInstance()->moveDesktopIconsTo(rect.x(), rect.y(), rect.width(), rect.height());
    }
}

void MainWindowViewModel::recoverDesktop()
{
    setCleaning(false);
    DesktopIconManager::getInstance()->restoreDesktopIcons();
}
