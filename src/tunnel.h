#ifndef TUNNEL_H
#define TUNNEL_H

#include <string>
#include <iostream>

#include <cstring>
#include <cerrno>
#include <csignal>

#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>


struct Data
{
    int srcfd;
    int dstfd;

    event_base *base;
    event *connev;

    bool connected;
    socklen_t socklen;
    const sockaddr *raddr;

    Data(event_base *_base, const sockaddr* raddr, socklen_t _socklen);
    ~Data();
};

struct Buffer
{
    const int rfd;
    const int wfd;

    const int rpipe;
    const int wpipe;

    event_base *base; 
    event *read_ev;
    event *write_ev;

    Buffer(event_base* _base, const int _rfd, const int _wfd, const int _rpipe, const int _wpipe);
    ~Buffer();
    void init();
};

struct Tunnel
{
    // event
    event_base *base;
    
    // address
    sockaddr* laddr;
    sockaddr* raddr;
    socklen_t socklen;

    int lisfd;
    const static int blocksize = 4096;

    // callback
    static void read_callback(int, short, void*);
    static void write_callback(int, short, void*);
    static void accept_callback(int, short, void*);
    static void connect_callback(int, short, void*);

    // util
    static sockaddr* parse_addr(const std::string& addr);

    explicit Tunnel(event_base*, sockaddr*, sockaddr*, socklen_t);
    explicit Tunnel(event_base*, const std::string&, const std::string&);
    ~Tunnel();

    int init(); 
    void run();
};



#endif