#pragma once

#include <QObject>
#include <QRect>
#include <QString>

class MainWindowViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString windowTitle READ windowTitle CONSTANT)
    Q_PROPERTY(bool isCleaning READ isCleaning WRITE setCleaning NOTIFY cleaningStatusChanged)

public:
    explicit MainWindowViewModel(QObject* parent = nullptr);
    ~MainWindowViewModel() override;

public:
    QString windowTitle() const;

    bool isCleaning() const { return m_isCleaning; }
    void setCleaning(bool cleaning);

public slots:
    void clearDesktop(const QRect& rect);
    void windowMoved(const QRect& rect);
    void recoverDesktop();

signals:
    void cleaningStatusChanged(bool isCleaning);

private:
    bool m_isCleaning = false;
};