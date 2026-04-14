#include "tcp_connect/tcp_connect.h"

#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>

namespace tcp_connect
{
    TCPConnect::TCPConnect(const std::string &ip, const int &port)
    {
        server_ip_ = ip;
        server_port_ = port;
        max_retry_ = 3;
        retry_delay_ = 3;
        socket_ = -1;
        is_connect_ = false;
        message_id_ = 0;
    }

    TCPConnect::~TCPConnect()
    {
        tcp_disconnect();
    }

    void TCPConnect::tcp_disconnect()
    {
        if (socket_ >= 0)
        {
            close(socket_);
            socket_ = -1;
        }
        is_connect_ = false;
    }

    bool TCPConnect::tcp_init()
    {
        if (is_connect_)
        {
            tcp_disconnect();
        }
        int retry_count = 0;
        while (retry_count < max_retry_)
        {
            // 创建socket
            socket_ = socket(AF_INET, SOCK_STREAM, 0);
            if (socket_ < 0)
            {
                printf("创建socket失败 (尝试 %d/%d)\n", retry_count + 1, max_retry_);
                retry_count++;
                is_connect_ = false;
                std::this_thread::sleep_for(std::chrono::seconds(retry_delay_));
                continue;
            }
            // 设置服务器地址
            sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(server_port_);
            if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) <= 0)
            {
                printf("IP地址无效: %s (尝试 %d/%d)\n", server_ip_.c_str(), retry_count + 1, max_retry_);
                close(socket_);
                socket_ = -1;
                retry_count++;
                is_connect_ = false;
                std::this_thread::sleep_for(std::chrono::seconds(retry_delay_));
                continue;
            }
            // 设置连接超时
            timeval timeout;
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;
            setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            // 连接服务器
            if (connect(socket_, (const sockaddr *)&server_addr, sizeof(server_addr)) < 0)
            {
                printf("连接服务器失败 (尝试 %d/%d)\n", retry_count + 1, max_retry_);
                close(socket_);
                socket_ = -1;
                retry_count++;
                is_connect_ = false;
                std::this_thread::sleep_for(std::chrono::seconds(retry_delay_));
                continue;
            }
            is_connect_ = true;
            printf("成功连接到服务器\n");
            return true;
        }
        is_connect_ = false;
        printf("服务器连接失败\n");
        return false;
    }

    bool TCPConnect::ensure_connect()
    {
        if (!is_connect_)
        {
            return tcp_init();
        }

        char test_buf = 0;
        ssize_t result = recv(socket_, &test_buf, 1, MSG_PEEK | MSG_DONTWAIT);

        if (result == 0)
        {
            printf("连接已被服务器关闭\n");
            is_connect_ = false;
            return tcp_init();
        }
        else if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            printf("连接可能已失效 (错误: %s)\n", strerror(errno));
            is_connect_ = false;
            return tcp_init();
        }
        else
        {
            return true;
        }
    }

    std::string TCPConnect::create_asdu(std::string type)
    {
        // 以type:1007为例
        char time_buf[50];
        time_t now = time(0);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        std::string asdu = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        asdu += "<PatrolDevice>\n";
        asdu += "    <Type>" + type + "</Type>\n";
        asdu += "    <Command>1</Command>\n";
        asdu += "    <Time>" + std::string(time_buf) + "</Time>\n";
        asdu += "    <Items/>\n";
        asdu += "</PatrolDevice>";
        type_ = type;
        return asdu;
    }

    std::vector<uint8_t> TCPConnect::create_header(uint16_t asdu_length)
    {
        std::vector<uint8_t> header(16, 0);
        // 同步字符
        header[0] = 0xEB;
        header[1] = 0x90;
        header[2] = 0xEB;
        header[3] = 0x90;
        // ASDU长度（小端字节序）
        header[4] = asdu_length & 0xFF;        // 低字节
        header[5] = (asdu_length >> 8) & 0xFF; // 高字节
        // 报文ID（小端字节序）
        header[6] = message_id_ & 0xFF;        // 低字节
        header[7] = (message_id_ >> 8) & 0xFF; // 高字节
        // 预留8字节（0x00）
        for (int i = 8; i < 16; i++)
        {
            header[i] = 0x00;
        }
        return header;
    }

    bool TCPConnect::send_request(const std::string &type, std::vector<uint8_t> &res)
    {
        if (!ensure_connect())
        {
            printf("已断开连接\n");
            return false;
        }
        // 构建完整request
        std::string asdu = create_asdu(type);
        auto header = create_header(static_cast<uint16_t>(asdu.length()));
        std::vector<uint8_t> request;
        request.reserve(header.size() + asdu.length());
        request.insert(request.end(), header.begin(), header.end());
        request.insert(request.end(), asdu.begin(), asdu.end());

        int retry_count = 0;
        while (retry_count < max_retry_)
        {
            // 发送请求
            ssize_t bytes_sent = send(socket_, request.data(), request.size(), 0);
            if (bytes_sent < 0)
            {
                printf("发送请求失败 (尝试 %d/2): %s\n", retry_count + 1, strerror(errno));
                retry_count++;
                is_connect_ = false;
                ensure_connect();
                continue;
            }
            // 接收响应
            std::this_thread::sleep_for(std::chrono::seconds(1));
            res.clear();
            res.resize(8192);
            ssize_t bytes_received = recv(socket_, res.data(), res.size(), 0);
            if (static_cast<size_t>(bytes_received) >= res.size())
            {
                printf("数据过多，超出缓存区\n");
                return false;
            }
            if (bytes_received <= 0)
            {
                printf("接收响应失败 (尝试 %d/2)\n", retry_count + 1);
                retry_count++;
                continue;
            }
            else
            {
                res.resize(bytes_received);
                printf("接收成功，响应长度: %ld 字节\n", bytes_received);
                return true;
            }
        }
        printf("发送/接收失败，已达到最大重试次数\n");
        return false;
    }

    bool TCPConnect::parse_header(const std::vector<uint8_t> &res, std::string &asdu)
    {
        std::vector<uint8_t> response = res;
        if (response[0] != 0xEB || response[1] != 0x90 || response[2] != 0xEB || response[3] != 0x90)
        {
            printf("同步字符有误\n");
            return false;
        }
        uint16_t message_id = response[6] | (response[7] << 8);
        if (message_id != message_id_)
        {
            printf("报文id不一致\n");
            return false;
        }
        else
        {
            message_id += 1;
            message_id_ = message_id % 65535;
        }
        uint16_t asdu_length = response[4] | (response[5] << 8);
        if (asdu_length != (response.size() - 16))
        {
            printf("ASDU数据错误\n");
            return false;
        }
        else
        {
            asdu = std::string(response.begin() + 16, response.begin() + 16 + asdu_length);
            return true;
        }
    }

    bool TCPConnect::parse_asdu(const std::string &asdu)
    {
        // 查找Type字段
        size_t type_start = asdu.find("<Type>");
        size_t type_end = asdu.find("</Type>");
        std::string type = asdu.substr(type_start + 6, type_end - type_start - 6);
        if (type == "0")
        {
            size_t error_start = asdu.find("<ErrorCode>");
            size_t error_end = asdu.find("</ErrorCode>");
            std::string errorcode = asdu.substr(error_start + 11, error_end - error_start - 11);
            printf("请求消息错误,error:%s\n", errorcode.c_str());
            return false;
        }
        else if (type != type_)
        {
            printf("type不一致\n");
            return false;
        }
        else
        {
            printf("type:%s\n", type.c_str());
        }
        // 查找Value字段
        std::string value;
        size_t value_start = asdu.find("<Value>");
        size_t value_end = asdu.find("</Value>");
        if (value_start == std::string::npos || value_end == std::string::npos)
        {
            printf("未找到<Value>字段\n");
            return false;
        }
        else
        {
            value = asdu.substr(value_start + 7, value_end - value_start - 7);
        }

        // 查找Status字段
        std::string status;
        size_t status_start = asdu.find("<Status>");
        size_t status_end = asdu.find("</Status>");
        if (status_start == std::string::npos || status_end == std::string::npos)
        {
            printf("未找到<Status>字段\n");
            return false;
        }
        else
        {
            status = asdu.substr(status_start + 8, status_end - status_start - 8);
        }

        // 查找ErrorCode字段
        std::string errorcode;
        size_t errorcode_start = asdu.find("<ErrorCode>");
        size_t errorcode_end = asdu.find("</ErrorCode>");
        if (errorcode_start == std::string::npos || errorcode_end == std::string::npos)
        {
            printf("未找到<ErrorCode>字段\n");
            return false;
        }
        else
        {
            errorcode = asdu.substr(errorcode_start + 11, errorcode_end - errorcode_start - 11);
        }
        return true;
    }
}