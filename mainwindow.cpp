#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include<QHostInfo>
#include<QMessageBox>
#include<QFileDialog>
#include<QProgressDialog>
#include<QTimer>

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

    ui->actStopListen->setEnabled(false);

    ui->actDisconnect->setEnabled(false);
    ui->actSendFile->setEnabled(false);

    server=new QTcpServer(this);
    connect(server,&QTcpServer::newConnection,this,&MainWindow::newConnection);

    sendTimer = new QTimer;
    connect(sendTimer,&QTimer::timeout,this,&MainWindow::sendFile);
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
    int port=ui->leRcvPort->text().toInt();
    if(port<1024||port>65535)
    {
        QMessageBox::critical(this,"错误","接受端口号无效");
        return;
    }
    server->listen(QHostAddress::Any,port);

    ui->actListen->setEnabled(false);
    ui->actStopListen->setEnabled(true);

    ui->pteLogs->appendPlainText("开始监听...");
}

void MainWindow::on_actStopListen_triggered()
{
    server->close();

    ui->actListen->setEnabled(true);
    ui->actStopListen->setEnabled(false);
    ui->actSendFile->setEnabled(false);

    ui->pteLogs->appendPlainText("停止监听...");
}

void MainWindow::on_actSendFile_triggered()
{
    QString path=QFileDialog::getOpenFileName(this,"选择文件",".","所有文件(*.*)");
    if(path.isEmpty())
    {
        return;
    }

    QFileInfo info(path);
    if(file!=nullptr)
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

    rstBytes=totalBytes=file->size();
    fileName=info.fileName();
    isSending=true;
    isTransfering=true;
    ui->pteLogs->appendPlainText("开始发送文件:"+fileName);
    ui->pteLogs->appendPlainText("文件大小:"+QString::number(totalBytes/1024.0/1024.0)+"MB");

    ui->actSendFile->setEnabled(false);
    ui->actListen->setEnabled(false);
    ui->actStopListen->setEnabled(false);

    sendTimer->start(1);

    if(pgDilg==nullptr){
        pgDilg=new QProgressDialog("正在发送","隐藏进度条",0,100);
    }
}

void MainWindow::on_actConnect_triggered()
{
    QString str=ui->leDstIp->text();
    int port=ui->leDstPort->text().toInt();
    if(port<1024||port>65535)
    {
        QMessageBox::critical(this,"错误","目标端口号无效");
        return;
    }
    if(client==nullptr)
    {
        client=new QTcpSocket(this);
        connect(client,&QTcpSocket::readyRead,this,&MainWindow::newData);
        connect(client,&QTcpSocket::disconnected,this,&MainWindow::disconnected);
    }
    client->connectToHost(str,port);

    if(!client->waitForConnected(5000))
    {
        QMessageBox::critical(this,"连接错误",client->errorString());
        ui->pteLogs->appendPlainText("连接错误:"+client->errorString());
        return;
    }

    ui->pteLogs->appendPlainText("连接成功:"+str+":"+QString::number(port));

    ui->actConnect->setEnabled(false);
    ui->actSendFile->setEnabled(true);
    ui->actDisconnect->setEnabled(true);
}

void MainWindow::on_actDisconnect_triggered()
{
    client->abort();
    delete client;
    client=nullptr;

    ui->pteLogs->appendPlainText("断开连接");

    isTransfering=false;
    sendTimer->stop();
    if(file && file->isOpen()){
        file->close();
        delete file;
        file=nullptr;
    }

    ui->actConnect->setEnabled(true);
    ui->actSendFile->setEnabled(false);
    ui->actListen->setEnabled(true);
    ui->actDisconnect->setEnabled(false);
}

void MainWindow::sendFile()
{
    //因为一毫秒发送一次,这里可以粗略设置发送速度
    //buf的大小*1000即为每秒发送速度
    //65536*1000 = 每秒64MB
    QByteArray buf=file->read(65536);
    rstBytes -= buf.size();
    qsizetype n=buf.size();
    while(n>0)
        n-=client->write(buf.last(n));
    ui->pteLogs->appendPlainText(QString("发送%1Bytes数据,剩余%2Bytes")
                                     .arg(buf.size()).arg(rstBytes));
    pgDilg->setValue(100.0*(totalBytes-rstBytes)/totalBytes);
    if(rstBytes>0)
        return;

    file->close();
    isTransfering=false;
    sendTimer->stop();
    ui->pteLogs->appendPlainText("文件发送完成:"+fileName);
}

void MainWindow::newConnection()
{
    client=server->nextPendingConnection();
    connect(client,&QTcpSocket::readyRead,this,&MainWindow::newData);
    connect(client,&QTcpSocket::disconnected,this,&MainWindow::disconnected);

    ui->pteLogs->appendPlainText("新连接:"+client->peerAddress().toString()+
                                 ":"+QString::number(client->peerPort()));

    ui->actConnect->setEnabled(false);
    ui->actSendFile->setEnabled(true);
    ui->actDisconnect->setEnabled(true);

    // allow only one connection
    server->close();
    ui->actListen->setEnabled(false);
    ui->actStopListen->setEnabled(false);
}

void MainWindow::newData()
{
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
        ui->actSendFile->setEnabled(false);
        totalBytes=rstBytes=header.size;

        isSending=false;
        isTransfering=true;

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
        if(pgDilg==nullptr){
            pgDilg=new QProgressDialog("正在发送","隐藏进度条",0,100);
        }
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
        }
    }else{
        QByteArray data=client->readAll();

        rstBytes-=data.size();
        ui->pteLogs->appendPlainText(QString("收到%1Bytes数据,剩余%2Bytes").arg(data.size()).arg(rstBytes));
        file->write(data);
        pgDilg->setValue(100.0*(totalBytes-rstBytes)/totalBytes);
        if(rstBytes>0)
            return;

        ui->actSendFile->setEnabled(true);
        ui->pteLogs->appendPlainText("文件接收完成:"+fileName);

        isTransfering=false;
        file->close();
    }
}

void MainWindow::disconnected()
{
    ui->pteLogs->appendPlainText("连接断开");
    ui->actConnect->setEnabled(true);
    ui->actSendFile->setEnabled(false);
    ui->actDisconnect->setEnabled(false);

    client->close();
    delete client;
    client=nullptr;

    isTransfering=false;
    sendTimer->stop();
    if(file && file->isOpen()){
        file->close();
        delete file;
        file=nullptr;
    }

    ui->actConnect->setEnabled(true);
    ui->actListen->setEnabled(true);
}
