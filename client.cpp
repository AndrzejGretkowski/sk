#include "client.h"
#include "command.h"

void send_command(QTcpSocket* socket, char command, int arg1, int arg2, int arg3)
{
    auto cmd = create_cmd(command, arg1, arg2, arg3);
    socket->write(cmd.data(), cmd.size());
    socket->waitForBytesWritten();
}

Client::Client(QWidget *parent)
    : QDialog(parent)
    , host_edit(new QLineEdit)
    , port_edit(new QLineEdit)
    , pass_edit(new QLineEdit)
    , connect_button(new QPushButton(tr("Connect")))
    , tcp_socket(new QTcpSocket(this))
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    stacked_layout = new QStackedLayout;

    port_edit ->setValidator(new QIntValidator(1, 65535, this));

    auto host_label = new QLabel(tr("Server name:"));
    host_label->setBuddy(host_edit);
    auto port_label = new QLabel(tr("Server port:"));
    port_label->setBuddy(port_edit);
    auto pass_label = new QLabel(tr("Password:"));
    pass_label->setBuddy(pass_edit);

    connect_button->setDefault(true);
    connect_button->setEnabled(false);

    auto quit_button = new QPushButton(tr("Quit"));

    auto buttonBox = new QDialogButtonBox;
    buttonBox->addButton(connect_button, QDialogButtonBox::ActionRole);
    buttonBox->addButton(quit_button, QDialogButtonBox::RejectRole);

    connect(connect_button, &QAbstractButton::clicked,
            this, &Client::try_login);
    connect(quit_button, &QAbstractButton::clicked,
            this, &QWidget::close);
    connect(tcp_socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(display_error(QAbstractSocket::SocketError)));
    connect(host_edit, &QLineEdit::textChanged,
            this, &Client::enable_connect);
    connect(port_edit, &QLineEdit::textChanged,
            this, &Client::enable_connect);


    QWidget *firstWidget = new QWidget;

    main_layout = new QGridLayout;

    main_layout->addWidget(host_label, 0, 0);
    main_layout->addWidget(host_edit, 0, 1);
    main_layout->addWidget(port_label, 1, 0);
    main_layout->addWidget(port_edit, 1, 1);
    main_layout->addWidget(pass_label, 2, 0);
    main_layout->addWidget(pass_edit, 2, 1);
    main_layout->addWidget(buttonBox, 3, 0, 1, 2);    

    setWindowTitle("LOG IN");
    host_edit->setFocus();

    firstWidget->setLayout(main_layout);

    stacked_layout->addWidget(firstWidget);

    QWidget *secondWidget = new QWidget;

    bigEditor = new QTextEdit;
    bigEditor->setReadOnly(true);

    auto buttonBox2 = new QDialogButtonBox;
    auto quit_button2 = new QPushButton(tr("Disconnect"));
    buttonBox2->addButton(quit_button2, QDialogButtonBox::ActionRole);
    //buttonBox2->addButton(quit_button, QDialogButtonBox::RejectRole);

    connect(quit_button2, &QAbstractButton::clicked,
            this, &Client::disconnect_from_host);

    logged_layout = new QGridLayout;
    logged_layout->addWidget(bigEditor);
    logged_layout->addWidget(buttonBox2);

    secondWidget->setLayout(logged_layout);

    stacked_layout->addWidget(secondWidget);

    setLayout(stacked_layout);

    stacked_layout->setCurrentIndex(0);

    mTimer = new QTimer(this);
    mTimer->setSingleShot(true);

    QDir dir;
    dir_files = dir.entryList();
}

void Client::ready_read()
{
    if (tcp_socket->bytesAvailable() < 12){
        return;
    }

    while(tcp_socket->bytesAvailable() >= 12){

        QByteArray data = tcp_socket->read(12);
        auto cmd = read_cmd(convert_from_bytes(data));

        qDebug() << std::get<0>(cmd) << std::get<1>(cmd)  << std::get<2>(cmd) << std::get<3>(cmd);

        if(std::get<0>(cmd) >= 'a' && std::get<0>(cmd) <= 'z' &&
                std::get<2>(cmd) >= 0 && std::get<3>(cmd) >= 0)
        {
            switch(std::get<0>(cmd)){
                case(LOGGED_IN):

                    logged_into_server(std::get<2>(cmd));

                    break;
                case(WRONG_PASS):

                    wrong_password();

                    break;
                case(SENDING_TASK):

                    add_task(std::get<1>(cmd), std::get<2>(cmd), std::get<3>(cmd));

                    break;
                case(READY_FOR_ANSWER):

                    task_to_send(std::get<1>(cmd), std::get<2>(cmd), std::get<3>(cmd));

                    break;
                case(ABORT_SENDING):

                    task_for_delete(std::get<1>(cmd), std::get<2>(cmd), std::get<3>(cmd));

                    break;
                case(SHUTTING_DOWN):

                    display_error(QAbstractSocket::RemoteHostClosedError);
                    disconnect_from_host();
                    //TODO close new layout

                    break;
                default:
                    qDebug("Unknown command recieved! Clearing buffer!");
                    tcp_socket->readAll();
                    disconnect_from_host();
                    break;
            }
        }
        else
        {
            qDebug("Unknown command recieved! Clearing buffer!");
            tcp_socket->readAll();
            disconnect_from_host();
        }
    }
}

void Client::main_loop()
{
    if(tcp_socket->state() == QTcpSocket::ConnectedState)
    {
        send_answers();

        run_tasks();

        delete_tasks();

        check_for_complete();

        send_alive_signal();
    }

    reset_timer();

    //qDebug("If you see this, program is doing... something.");

}

void Client::try_login()
{
    connect_button->setEnabled(false);
    tcp_socket->abort();
    tcp_socket->connectToHost(QHostAddress(host_edit->text()),
                             port_edit->text().toInt()); 

    if(this->tcp_socket->waitForConnected(2000))
    {
        qDebug("Connected!");

        connect(tcp_socket, &QAbstractSocket::readyRead,
                this, &Client::ready_read);

        send_command(tcp_socket, TRY_LOGIN, 0, pass_edit->text().toStdString().size(), 0);

        tcp_socket->write(pass_edit->text().toStdString().data());
        tcp_socket->waitForBytesWritten();

        qDebug("Waiting for confirmation command!");

        loginTimer = new QTimer(this);
        connect(loginTimer, &QTimer::timeout, this, &Client::login_timeout);
        loginTimer->setSingleShot(true);
        loginTimer->start(1000);
    }else{

        qDebug("Connection timeout!");
        display_error(QAbstractSocket::SocketTimeoutError);
        connect_button->setEnabled(true);
        tcp_socket->abort();

    }
}

void Client::add_task(int id, int cmd_len, int file_len){

    qDebug("New task!");
    bigEditor->append("New task!\n");

    int loop_time_secs = 0;
    bool task_read = false;
    bool cmd_read = false;
    QString cmd_line = "";
    int remaining_bytes = file_len;

    QString filename = QString("task%1").arg(id);

    QFile file(filename);

    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)){
        qDebug("Can't write to file!");
        bigEditor->append("Can't write to file!\n");
        file.errorString();

    }
    else
    {

        QDataStream out(&file);

        while(loop_time_secs < DATA_TIME_SECS && !task_read)
        {
            if(tcp_socket->bytesAvailable() >= cmd_len && !cmd_read){
                QByteArray data_cmd_line = tcp_socket->read(cmd_len);
                cmd_line = QString(data_cmd_line.data());
                cmd_read = true;
                loop_time_secs = 0;

            }else{
                if(!cmd_read){
                    if(tcp_socket->waitForReadyRead(1000))
                    {
                        loop_time_secs = 0;
                    }
                    else
                    {
                        loop_time_secs++;
                    }
                }
                else
                {
                    while(remaining_bytes > 0 && loop_time_secs < DATA_TIME_SECS)
                    {
                        if(tcp_socket->bytesAvailable() > 0 && remaining_bytes > 0)
                        {
                            int bytes_read = tcp_socket->bytesAvailable();

                            if(bytes_read > remaining_bytes)
                                bytes_read = remaining_bytes;

                            if(bytes_read > DATA_BUFFOR_SIZE)
                                bytes_read = DATA_BUFFOR_SIZE;

                            QByteArray data_file = tcp_socket->read(bytes_read);

                            out.writeRawData(data_file.data(), bytes_read);

                            remaining_bytes = remaining_bytes - bytes_read;
                        }
                        else
                        {
                            if(tcp_socket->waitForReadyRead(1000))
                            {
                                loop_time_secs = 0;
                            }
                            else
                            {
                                loop_time_secs++;
                            }
                        }
                    }

                    if(remaining_bytes == 0){
                        task_read = true;
                    }
                }
            }
        }

        file.close();


        if(loop_time_secs >= DATA_TIME_SECS && !task_read){
            qDebug("Data recieved timeout, clearing buffer!");
            tcp_socket->readAll();
            disconnect_from_host();
        }

        if(cmd_read && task_read){
            task* new_task = new task(id, filename, cmd_line);
            tasks.push_back(new_task);
            qDebug("Task added!");
            bigEditor->append(tr("Task number %1 added!\n").arg(id));
        }
    }
}

void Client::task_to_send(int id, int check1, int check2)
{
    if(answerTimer->isActive()){
        answerTimer->stop();

    }

    for(auto tsk : tasks){
        if(check1 == check2 && check2 == 0){
            if(tsk->id == id){
                tsk->server_ready = true;
                qDebug("Task ready! Task number is: ");
                qDebug() << id;
                bigEditor->append(tr("Task number %1 is server ready!\n").arg(id));
            }
        }
    }
}

void Client::task_for_delete(int id, int check1, int check2){

    for(auto tsk : tasks){
        if(check1 == check2 && check2 == 0){
            if(tsk->id == id){
                tsk->server_ready = false;
                tsk->for_delete = true;
                qDebug("Task aborted! Task number is: ");
                qDebug() << id;
            }
        }
    }

    sending_task = false;
}

void Client::send_answers()
{
    for(auto tsk : tasks)
    {
        if(tsk->client_ready && tsk->server_ready && !tsk->for_delete){

            QFile file(tsk->result_path);
            if(!file.open(QIODevice::ReadOnly)){

                qDebug() << file.errorString();
                if(!file.setPermissions(QFileDevice::ReadOther)){
                    qDebug("Opening file failed.");
                    //send_command(tcp_socket, PROGRAM_FAILED, tsk->id, 0, 0);
                    disconnect_from_host();
                    tsk->for_delete = true;
                }
            }else{
                qDebug("Sending answer!");
                bigEditor->append(tr("Sending task %1 answer!\n").arg(tsk->id));

                int remaining_bytes = file.size();

                while(remaining_bytes > 0){
                    int bytes_to_send = DATA_BUFFOR_SIZE;

                    if(remaining_bytes < bytes_to_send)
                        bytes_to_send = remaining_bytes;

                    QByteArray ans = file.read(bytes_to_send);

                    tcp_socket->write(ans, ans.size());

                    remaining_bytes = remaining_bytes - bytes_to_send;
                }
                //TODO: TESTING!
                //tcp_socket->waitForBytesWritten();
                qDebug("Answer send!");
                bigEditor->append(tr("Answer for task %1 send!\n").arg(tsk->id));
                tsk->for_delete = true;
            }
            sending_task = false;
        }
    }
}

void Client::run_tasks()
{
    for(std::size_t i = 0; i < tasks.size(); i++)
    {
        auto tsk = tasks[i];
        if(!tsk->client_ready)
        {
            qDebug("Running task!");            
            QString filename = QString("answer%1").arg(tsk->id);
            tsk->result_path = filename;

            QString sys_cmd = tsk->cmd_line.replace(PROGRAM_NAME, tsk->program_path);

            tsk->process = new QProcess();
            tsk->process->start("sh", QStringList()<<"-c"<<sys_cmd);
            tsk->process->waitForStarted();

            //tsk->result = system(sys_cmd.toStdString().c_str());

            tsk->client_ready = true;
            qDebug("Program started! Task number is: ");
            qDebug() << tsk->id;

            bigEditor->append(tr("Program for task %1 started!\n").arg(tsk->id));

        }
    }
}

void Client::delete_tasks()
{
    for(int i = tasks.size() -1 ; i >= 0; i--){
        auto tsk = tasks[i];
        if(tsk->for_delete){
            if(QFile::remove(tsk->program_path)){
                qDebug("Program deleted! Task number is: ");
                qDebug() << tsk->id;
            }
            if(QFile::remove(tsk->result_path)){
                qDebug("Result deleted! Task number is: ");
                qDebug() << tsk->id;
            }

            tasks.erase(tasks.begin() + i);
        }
    }
}

void Client::check_for_complete(){

    for(std::size_t i = 0; i < tasks.size(); i++)
    {
        auto tsk = tasks[i];

        if(tsk->process->state() == QProcess::NotRunning && tsk->client_ready && !sending_task && !tsk->for_delete)
        {

            auto error = tsk->process->readAllStandardError();

            if(tsk->process->exitStatus() == QProcess::NormalExit && error.size() == 0){

                qDebug("Saving answer!");
                QFile file(tsk->result_path);
                if(!file.open(QIODevice::ReadWrite)){
                    qDebug() << file.errorString();
                    qDebug("Opening file failed.");
                    bigEditor->append(tr("Opening file for task %1 failed!\n").arg(tsk->id));
                    send_command(tcp_socket, PROGRAM_FAILED, tsk->id, 0, 0);
                    tsk->for_delete = true;
                }else{
                    QDataStream out(&file);
                    while(tsk->process->bytesAvailable() > 0){
                        QByteArray data = tsk->process->readAll();
                        out.writeRawData(data.data(), data.size());
                    }

                    send_command(tcp_socket, PLS_DONT_KILL_ME, 0, 0, 0);
                    sending_task = true;
                    send_command(tcp_socket, SENDING_ANSWER, tsk->id, file.size(), 0);
                    answerTimer->start(5000);
                }
                qDebug("Task finished!");
                bigEditor->append(tr("Task %1 finished!\n").arg(tsk->id));

            }
            else{
                qDebug("Program failed");
                bigEditor->append(tr("Program for task %1 failed!\n").arg(tsk->id));
                send_command(tcp_socket, PROGRAM_FAILED, tsk->id, 0, 0);
                tsk->for_delete = true;
            }
        }
    }
}

void Client::disconnect_from_host()
{
    tcp_socket->disconnectFromHost();
    tcp_socket->abort();

    if(logged_in){
        disconnect(aliveTimer, &QTimer::timeout, 0, 0);
        disconnect(mTimer, &QTimer::timeout, 0, 0);
        disconnect(answerTimer, &QTimer::timeout, 0, 0);
    }

    stacked_layout->setCurrentIndex(0);
    connect_button->setEnabled(true);
    tasks.clear();
    bigEditor->clear();
    sending_task = false;
    clean_up_dir();
}

void Client::enable_connect()
{
    connect_button->setEnabled(!host_edit->text().isEmpty() &&
                                 !port_edit->text().isEmpty());

}

void Client::reset_timer()
{
    mTimer->start(1000);
}

void Client::logged_into_server(int keep_alive_time)
{

    qDebug("Logged in!");
    alive_time = keep_alive_time * 750;
    logged_in = true;

    connect(mTimer, &QTimer::timeout, this, &Client::main_loop);
    reset_timer();

    aliveTimer = new QTimer(this);
    aliveTimer->setSingleShot(true);

    if(alive_time > 0){
        connect(aliveTimer, &QTimer::timeout, this, &Client::alive_signal_timeout);
    }

    aliveTimer->start(alive_time);

    answerTimer = new QTimer(this);
    connect(answerTimer, &QTimer::timeout, this, &Client::ready_timeout);
    answerTimer->setSingleShot(true);

    stacked_layout->setCurrentIndex(1);
}

void Client::wrong_password()
{

    qDebug("Wrong password!");
    //TODO custom message box?
    loginTimer->stop();
    disconnect(loginTimer, &QTimer::timeout, 0, 0);
    disconnect(tcp_socket, &QAbstractSocket::readyRead, 0, 0);

    disconnect_from_host();

    //display_error(QAbstractSocket::ConnectionRefusedError);

}

void Client::ready_timeout()
{
    disconnect_from_host();
}

void Client::login_timeout()
{
    if(!logged_in)
    {
        tcp_socket->abort();
        disconnect(tcp_socket, &QAbstractSocket::readyRead, 0, 0);
        disconnect(loginTimer, &QTimer::timeout, 0, 0);
        qDebug("Login timeout!");
        //display_error(QAbstractSocket::SocketTimeoutError);
        connect_button->setEnabled(true);
    }
}

void Client::alive_signal_timeout(){
    send_alive = true;
}

void Client::send_alive_signal()
{
    if(send_alive && !sending_task){
        qDebug("Sending alive signal!");
        bigEditor->append(tr("Sending alive signal!\n"));

        send_command(tcp_socket, PLS_DONT_KILL_ME, 0, 0, 0);

        if(tcp_socket->ConnectedState == QAbstractSocket::UnconnectedState){
            disconnect_from_host();
        }

        send_alive = false;
        aliveTimer->start(alive_time);
    }
}

void Client::display_error(QAbstractSocket::SocketError socketError)
{
    stacked_layout->setCurrentIndex(0);

    switch (socketError) {
    case QAbstractSocket::RemoteHostClosedError:
        QMessageBox::information(this,tr("Error"),tr("Host closed connection!"));
        break;
    case QAbstractSocket::HostNotFoundError:
        QMessageBox::information(this,tr("Error"),tr("Host not found!"));
        break;
    case QAbstractSocket::ConnectionRefusedError:
        QMessageBox::information(this,tr("Error"),tr("Host denied access!"));
        break;
    case QAbstractSocket::SocketTimeoutError:
        QMessageBox::information(this,tr("Error"),tr("Connection timeout!"));
        break;
    default:
        QMessageBox::information(this,tr("Error"),tr("The following error occurred: %1.")
                                 .arg(tcp_socket->errorString()));
    }

    disconnect_from_host();
    connect_button->setEnabled(true);
}

void Client::clean_up_dir()
{
    QDir dir;
    auto current_files = dir.entryList();

    for(auto i = 0; i < current_files.size(); i++){
        if (!dir_files.contains(current_files[i]))
            dir.remove(current_files[i]);
    }
}
