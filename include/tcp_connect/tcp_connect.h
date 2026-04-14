#pragma once
#include <string>
#include <vector>

namespace tcp_connect
{
    class TCPConnect
    {
    private:
        std::string server_ip_; // 服务器地址
        int server_port_;       // 服务器端口
        int max_retry_;         // 最大重连次数
        int retry_delay_;       // 重连延迟时间
        int socket_;            // socket句柄
        bool is_connect_;       // 是否连接
        uint16_t message_id_;   // 报文id，从0开始递增
        std::string type_;      // 任务类型

    public:
        TCPConnect(const std::string &ip, const int &port);
        ~TCPConnect();
        bool tcp_init();
        void tcp_disconnect();
        bool ensure_connect();
        std::string create_asdu(std::string type);
        std::vector<uint8_t> create_header(uint16_t asdu_length);
        bool send_request(const std::string &type, std::vector<uint8_t> &res);
        bool parse_header(const std::vector<uint8_t> &res, std::string &asdu);
        bool parse_asdu(const std::string &asdu);
    };
}