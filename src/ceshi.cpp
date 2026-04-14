#include "tcp_connect/tcp_connect.h"
#include <memory>
int main(int argc, char **argv)
{
    auto tcp_connect_ptr = std::make_shared<tcp_connect::TCPConnect>("192.168.1.106", 30000);
    std::vector<uint8_t> response;
    std::string asdu;
    if (tcp_connect_ptr->send_request("1007", response))
    {
        if (tcp_connect_ptr->parse_header(response, asdu))
        {
            if (tcp_connect_ptr->parse_asdu(asdu))
            {
                printf("获取成功\n");
            }
        }
    }
    return 0;
}