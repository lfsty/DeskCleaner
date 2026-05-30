#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include <mainwindowviewmodel.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    qmlRegisterType<MainWindowViewModel>("DeskCleaner", 1, 0, "MainWindowViewModel");

    QQmlApplicationEngine engine;
    const QUrl url("qrc:/qml/mainwindow.qml");
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url](QObject *obj, const QUrl &objUrl)
                     {
                         if (!obj && url == objUrl)
                         {
                             QCoreApplication::exit(-1);
                         }
                     });
    engine.load(url);

    return app.exec();
}