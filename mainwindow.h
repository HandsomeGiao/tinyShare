#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include<QTcpServer>
#include<QTcpSocket>
#include<QFile>
#include<QProgressDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_btnGetIp_clicked();

    void on_actListen_triggered();

    void on_actStopListen_triggered();

    void on_actSendFile_triggered();

    void on_actConnect_triggered();

    void on_actDisconnect_triggered();

private:
    Ui::MainWindow *ui;

    //network
    QTcpServer* server=nullptr;
    QTcpSocket* client=nullptr;
    QFile* file=nullptr;
    bool isTransfering=false;
    bool isSending=false;
    quint64 totalBytes,rstBytes;
    QString fileName;

    QTimer* sendTimer=nullptr;

    QProgressDialog* pgDilg=nullptr;
private:
    void sendFile();
private slots:
    void newConnection();
    void newData();
    void disconnected();
};
#endif // MAINWINDOW_H
