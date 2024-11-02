#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include<QTcpServer>
#include<QTcpSocket>
#include<QFile>
#include<QProgressDialog>
#include<QElapsedTimer>

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
    // 是否正在传输文件
    bool isTransfering=false;
    // 是否作为发送者在传输文件
    bool isSending=false;

    quint64 totalBytes,rstBytes;
    QString fileName;

    //buffer size = 100MB
    quint64 RecvBufferSize=100*1024*1024;
    quint64 DataBlockSize=1024*64;

    QTimer* sendTimer=nullptr;
    QElapsedTimer* elpsdTimer=nullptr;

    QProgressDialog* pgDilg=nullptr;
private:
    void sendFile();
private slots:
    void newConnection();
    void newData();
    void disconnected();
    void bytesWritten(qint64 t);
};
#endif // MAINWINDOW_H
