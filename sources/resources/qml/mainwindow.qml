import QtQuick
import QtQuick.Controls
import DeskCleaner 1.0

ApplicationWindow {
    id: mainWindow
    readonly property int fixedWindowWidth: 400
    readonly property int fixedWindowHeight: 400

    onXChanged: {
        if (visible) {
            viewModel.windowMoved(mainWindow.currentWindowRect())
        }
    }

    onYChanged: {
        if (visible) {
            viewModel.windowMoved(mainWindow.currentWindowRect())
        }
    }

    function currentWindowRect() {
        const devicePixelRatio = mainWindow.screen ? mainWindow.screen.devicePixelRatio : 1.0
        return Qt.rect(
                    Math.floor(mainWindow.x * devicePixelRatio),
                    Math.floor(mainWindow.y * devicePixelRatio),
                    Math.ceil(mainWindow.width * devicePixelRatio) - 32,
                    Math.ceil(mainWindow.height * devicePixelRatio) - 32)
    }

    MainWindowViewModel {
        id: viewModel
    }

    visible: true
    width: fixedWindowWidth
    height: fixedWindowHeight
    minimumWidth: fixedWindowWidth
    maximumWidth: fixedWindowWidth
    minimumHeight: fixedWindowHeight
    maximumHeight: fixedWindowHeight
    title: viewModel.windowTitle

    Rectangle {
        anchors.fill: parent
        color: "lightblue"

        Column {
            anchors.centerIn: parent
            spacing: 16
            width: parent.width * 0.6

            Button {
                width: parent.width
                visible: !viewModel.isCleaning
                text: "清理桌面"
                onClicked: viewModel.clearDesktop(mainWindow.currentWindowRect())
            }

            Button {
                width: parent.width
                visible: viewModel.isCleaning
                text: "重置吧"
                onClicked: viewModel.recoverDesktop()
            }
        }
    }
}
