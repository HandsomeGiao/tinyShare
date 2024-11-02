#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include<QHostInfo>
#include<QMessageBox>
#include<QFileDialog>
#include<QProgressDialog>
#include<QTimer>
#include<thread>

struct FileHeader
{
    quint64 size;
    char name[1024];
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    server=new QTcpServer(this);
    connect(server,&QTcpServer::newConnection,this,&MainWindow::newConnection);

    sendTimer = new QTimer;
    connect(sendTimer,&QTimer::timeout,this,&MainWindow::sendFile);

    elpsdTimer = new QElapsedTimer;

    //ui
    ui->actDisconnect->setEnabled(false);
    ui->actStopListen->setEnabled(false);
    ui->actSendFile->setEnabled(false);
}

MainWindow::~MainWindow()
{
    delete ui;

    if(server!=nullptr && server->isListening())
    {
        server->close();
        delete server;
    }

    if(client!=nullptr)
    {
        client->close();
        delete client;
    }
}

void MainWindow::on_btnGetIp_clicked()
{
    ui->pteLogs->appendPlainText("所有本地IPV4地址如下:");
    auto allAddr=QHostInfo::fromName(QHostInfo::localHostName());
    for(const auto& addr:allAddr.addresses())
    {
        if(addr.protocol()==QAbstractSocket::IPv4Protocol)
        {
            ui->pteLogs->appendPlainText(addr.toString());
        }
    }
}

void MainWindow::on_actListen_triggered()
{
    if(server->isListening()){
        QMessageBox::critical(this,"错误","服务器已经在监听,请先停止监听");
        return;
    }

    int port=ui->leRcvPort->text().toInt();
    if(port<1024||port>65535)
    {
        QMessageBox::critical(this,"错误","接受端口号无效");
        return;
    }
    server->listen(QHostAddress::Any,port);

    ui->pteLogs->appendPlainText("开始监听...");

    //ui
    ui->actListen->setEnabled(false);
    ui->actStopListen->setEnabled(true);
}

void MainWindow::on_actStopListen_triggered()
{
    server->close();
    ui->pteLogs->appendPlainText("停止监听...");

    //ui
    ui->actListen->setEnabled(true);
    ui->actStopListen->setEnabled(false);
}

void MainWindow::on_actSendFile_triggered()
{
    if(client==nullptr || !client->isOpen())
    {
        QMessageBox::critical(this,"错误","请先连接到用户");
        return;
    }else if(isTransfering){
        QMessageBox::critical(this,"错误","请等待当前文件传输完成");
        return;
    }

    QString path=QFileDialog::getOpenFileName(this,"选择文件",".","所有文件(*.*)");
    if(path.isEmpty())
    {
        return;
    }

    QFileInfo info(path);
    if(file!=nullptr && file->isOpen())
    {
        file->close();
        delete file;
    }
    file=new QFile(path);
    if(!file->open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this,"错误","无法打开文件");
        return;
    }

    //send header
    FileHeader header;
    header.size=file->size();
    strncpy(header.name,info.fileName().toUtf8().data(),info.fileName().toUtf8().size()+1);
    client->write((const char*)&header,sizeof(header));

    //total size 需要加上header的大小(对于发送方而言)
    rstBytes=totalBytes=file->size()+sizeof(FileHeader);
    fileName=info.fileName();

    isSending=true;
    isTransfering=true;

    ui->pteLogs->appendPlainText("开始发送文件:"+fileName);
    ui->pteLogs->appendPlainText("文件大小:"+QString::number(totalBytes/1024.0/1024.0)+"MB");

    sendTimer->start(1);
    elpsdTimer->restart();

    if(pgDilg==nullptr){
        pgDilg=new QProgressDialog("正在发送","隐藏进度条",0,100,this);
    }
    pgDilg->show();

    //ui
    ui->actSendFile->setEnabled(false);
}

void MainWindow::on_actConnect_triggered()
{
    if(client!=nullptr)
    {
        QMessageBox::critical(this,"错误","请先断开连接");
        return;
    }

    QString str=ui->leDstIp->text();
    int port=ui->leDstPort->text().toInt();
    if(port<1024||port>65535)
    {
        QMessageBox::critical(this,"错误","目标端口号无效");
        return;
    }

    client=new QTcpSocket(this);
    connect(client,&QTcpSocket::readyRead,this,&MainWindow::newData);
    connect(client,&QTcpSocket::disconnected,this,&MainWindow::disconnected);
    connect(client,&QTcpSocket::bytesWritten,this,&MainWindow::bytesWritten);

    client->setReadBufferSize(RecvBufferSize);
    client->connectToHost(str,port);

    if(!client->waitForConnected(5000))
    {
        QMessageBox::critical(this,"连接错误",client->errorString());
        ui->pteLogs->appendPlainText("连接错误:"+client->errorString());
        client->deleteLater();
        client=nullptr;
        return;
    }

    ui->pteLogs->appendPlainText("连接成功:"+str+":"+QString::number(port));

    //ui
    ui->actConnect->setEnabled(false);
    ui->actDisconnect->setEnabled(true);
    ui->actSendFile->setEnabled(true);
    ui->actListen->setEnabled(false);
    ui->actStopListen->setEnabled(false);
}

void MainWindow::on_actDisconnect_triggered()
{
    if(client==nullptr)
        return;

    client->deleteLater() ;
    client=nullptr;

    ui->pteLogs->appendPlainText("断开连接");

    isTransfering=false;

    sendTimer->stop();

    if(file){
        if(file->isOpen())
            file->close();
        file->deleteLater();
        file=nullptr;
    }

    //ui
    ui->actConnect->setEnabled(true);
    ui->actDisconnect->setEnabled(false);
    ui->actSendFile->setEnabled(false);
    ui->actListen->setEnabled(true);
    ui->actStopListen->setEnabled(false);
}

void MainWindow::sendFile()
{
    if(!client || !client->isOpen()|| !file || !file->isOpen() || file->atEnd())
    {
        ui->pteLogs->appendPlainText("错误进入sendFile函数");
        return;
    }

    QByteArray buf=file->read(DataBlockSize);
    qsizetype n=buf.size();

    while(n>0){
        auto t=client->write(buf.last(n));
        //控制发射速度
        if(t==0)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        n -= t;
        //ui->pteLogs->appendPlainText(QString("发送%1Bytes数据").arg(t));
    }
    //ui->pteLogs->appendPlainText(QString("发送%1Bytes数据,剩余%2Bytes".arg(buf.size()).arg(rstBytes));
}

void MainWindow::newConnection()
{
    if(client!=nullptr)
    {
        QMessageBox::critical(this,"错误","已经有一个连接了,请先断开连接");
        return;
    }

    client=server->nextPendingConnection();
    client->setReadBufferSize(RecvBufferSize);

    connect(client,&QTcpSocket::readyRead,this,&MainWindow::newData);
    connect(client,&QTcpSocket::disconnected,this,&MainWindow::disconnected);
    connect(client,&QTcpSocket::bytesWritten,this,&MainWindow::bytesWritten);

    ui->pteLogs->appendPlainText("新连接:"+client->peerAddress().toString()+
                                 ":"+QString::number(client->peerPort()));

    // allow only one connection
    server->close();

    //ui
    ui->actConnect->setEnabled(false);
    ui->actDisconnect->setEnabled(true);
    ui->actSendFile->setEnabled(true);
    ui->actListen->setEnabled(false);
    ui->actStopListen->setEnabled(false);
}

void MainWindow::newData()
{
    if(client==nullptr || !client->isOpen())
    {
        ui->pteLogs->appendPlainText("错误进入newData函数");
        return;
    }

    if(!isTransfering)
    {
        FileHeader header;
        int t=sizeof(header);
        while(t>0){
            t-=client->read((char*)&header,t);
        }
        fileName=QString::fromUtf8(header.name);
        ui->pteLogs->appendPlainText(QString("接收文件:%1,文件大小:%2MB")
                                         .arg(fileName)
                                         .arg(header.size/1024.0/1024.0));
        totalBytes=rstBytes=header.size;

        isSending=false;
        isTransfering=true;

        //timer
        elpsdTimer->restart();

        if(file!=nullptr)
        {
            file->close();
            delete file;
        }
        file=new QFile(fileName);
        if(!file->open(QIODevice::WriteOnly))
        {
            QMessageBox::critical(this,"错误","无法打开文件");
            isTransfering=false;
            return;
        }
        if(pgDilg!=nullptr){
            delete pgDilg;
        }
        pgDilg=new QProgressDialog("正在接受文件","隐藏进度条",0,100);
        pgDilg->show();
        //检测一下是否已经有文件数据被顺带发送了,在发送小文件时可能会出现这种情况
        {
            QByteArray data=client->readAll();
            if(data.size()==0)
                return;
            rstBytes-=data.size();
            ui->pteLogs->appendPlainText(QString("收到%1Bytes数据,剩余%2Bytes")
                                             .arg(data.size()).arg(rstBytes));
            file->write(data);
            if(rstBytes>0)
                return;
            ui->pteLogs->appendPlainText("文件接收完成:"+fileName);
            isTransfering=false;

            file->close();
            delete file;
            file=nullptr;

            //ui
            ui->actSendFile->setEnabled(true);
        }
    }else{
        QByteArray data=client->readAll();

        rstBytes -= data.size();
        //ui->pteLogs->appendPlainText(QString("收到%1Bytes数据,剩余%2Bytes").arg(data.size()).arg(rstBytes));
        int n=data.size();
        while(n>0){
            n-=file->write(data.last(n));
        }

        pgDilg->setValue(100.0*(totalBytes-rstBytes)/totalBytes);
        if(rstBytes>0)
            return;

        ui->pteLogs->appendPlainText("文件接收完成:"+fileName);
        ui->pteLogs->appendPlainText(QString("文件接受完成,总共耗时%1s").arg(elpsdTimer->elapsed()/1000.0));

        isTransfering=false;
        file->close();

        //ui
        ui->actSendFile->setEnabled(true);
    }
}

void MainWindow::disconnected()
{
    ui->pteLogs->appendPlainText("连接断开");

    if(client){
        client->deleteLater();
        client=nullptr;
    }

    isTransfering=false;
    sendTimer->stop();

    if(file){
        if(file->isOpen())
            file->close();
        file->deleteLater();
        file=nullptr;
    }

    //ui
    ui->actConnect->setEnabled(true);
    ui->actDisconnect->setEnabled(false);
    ui->actSendFile->setEnabled(false);
    ui->actListen->setEnabled(true);
}

void MainWindow::bytesWritten(qint64 t)
{
    qDebug()<<"bytesWritten "<<t<<"bytes";
    rstBytes-=t;
    pgDilg->setValue(100.0*(totalBytes-rstBytes)/totalBytes);

    if(rstBytes>0)
        return;
    //发送完成
    file->close();
    delete file;
    file=nullptr;

    isTransfering=false;

    sendTimer->stop();

    ui->pteLogs->appendPlainText(QString("文件发送完成,总共耗时:%1s").arg(elpsdTimer->elapsed()/1000.0));
    ui->pteLogs->appendPlainText("文件发送完成:"+fileName);

    //ui
    ui->actSendFile->setEnabled(true);
}
