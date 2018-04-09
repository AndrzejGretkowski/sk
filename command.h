#ifndef COMMAND_H
#define COMMAND_H

#include <array>
#include <memory>
#include <type_traits>
#include <tuple>
#include <iostream>
#include <iomanip>
#include <QTcpSocket>

constexpr char SERVER_ALIVE = 'a';
constexpr char LOGGED_IN = 'i';
constexpr char WRONG_PASS = 'w';
constexpr char SENDING_TASK = 't';
constexpr char ANSWER_ACK = 'x';
constexpr char READY_FOR_ANSWER = 'r';
constexpr char ABORT_SENDING = 'a';
constexpr char SHUTTING_DOWN = 'd';

constexpr char TRY_LOGIN = 'o';
constexpr char SENDING_ANSWER = 's';
constexpr char PROGRAM_FAILED = 'f';
constexpr char DISCONNECTING = 'h';
constexpr char NEW_TASK = 'n';
constexpr char PLS_DONT_KILL_ME = 'p';

using byte = unsigned char ;

std::array<char, 12> create_cmd(char cmd, int task_id = 0, int param2 = 0, int param3 = 0)
{
    std::array<char, 12> ret;

    ret[0] = cmd;
    ret[1] = (task_id >> 16) & 0xFF;
    ret[2] = (task_id >> 8) & 0xFF;
    ret[3] = task_id & 0xFF;
    ret[4] = (param2 >> 24) & 0xFF;
    ret[5] = (param2 >> 16) & 0xFF;
    ret[6] = (param2 >> 8) & 0xFF;
    ret[7] = param2 & 0xFF;
    ret[8] = (param3 >> 24) & 0xFF;
    ret[9] = (param3 >> 16) & 0xFF;
    ret[10] = (param3 >> 8) & 0xFF;
    ret[11] = param3 & 0xFF;

    return ret;
}

std::tuple<char, int, int, int> read_cmd(const std::array<char, 12>& buffer)
{
    const unsigned char* buf = reinterpret_cast<const unsigned char*>(buffer.data());

    char cmd = buf[0];
    int task_id = (buf[1] << 16) | (buf[2] << 8) | buf[3];
    int param2 = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
    int param3 = (buf[8] << 24) | (buf[9] << 16) | (buf[10] << 8) | buf[11];

    return std::make_tuple(cmd, task_id, param2, param3);
}

std::array<char,12> convert_from_bytes(QByteArray byte_array){
    std::array<char,12> out;

    for(int i = 0; i < 12; i++){
        out[i] = byte_array[i];
    }

    return out;
}


#endif // COMMAND_H
