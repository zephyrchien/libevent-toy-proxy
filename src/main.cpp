#include "tunnel.h"

int main(int argc, char **argv)
{
    if (argc != 3) return -1;
    std::string laddr = std::string(argv[1]);
    std::string raddr = std::string(argv[2]);
    event_base *base = event_base_new();

    Tunnel tun = Tunnel(base,laddr,raddr);
    if (tun.init() < 0)
    {
        event_base_free(base);
        return -1;
    }
    tun.run();
    event_base_free(base);
    return 0;
}