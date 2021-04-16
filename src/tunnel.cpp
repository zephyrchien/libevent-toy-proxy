#include "tunnel.h"

Data::Data(event_base *_base, const sockaddr* _raddr, socklen_t _socklen):
    base(_base), raddr(_raddr), socklen(_socklen), connected(false)
{

}

Data::~Data()
{
    event_free(connev);
}

Buffer::Buffer(event_base* _base, const int _rfd, const int _wfd, const int _rpipe, const int _wpipe):
    base(_base), rfd(_rfd), wfd(_wfd), rpipe(_rpipe), wpipe(_wpipe)
{

}

Buffer::~Buffer()
{
    event_free(read_ev);
    event_free(write_ev);
    close(rfd);
    close(wfd);
    close(rpipe);
    close(wpipe);
}

void Buffer::init()
{
    read_ev = event_new(base,rfd,EV_READ|EV_PERSIST,Tunnel::read_callback,this);
    write_ev = event_new(base,wfd,EV_WRITE,Tunnel::write_callback,this);
}


Tunnel::Tunnel(event_base *_base, sockaddr *_laddr, sockaddr *_raddr, socklen_t _socklen): 
    base(_base), laddr(_laddr), raddr(_raddr), socklen(_socklen)
{    
    lisfd = socket(PF_INET,SOCK_STREAM,0);
    int opt = 1;
    setsockopt(lisfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    evutil_make_socket_nonblocking(lisfd);
}


Tunnel::Tunnel(event_base *_base, const std::string& laddrstr, const std::string& raddrstr):
    base(_base)
{
    laddr = Tunnel::parse_addr(laddrstr);
    raddr = Tunnel::parse_addr(raddrstr);
    socklen = sizeof(sockaddr_in);
    lisfd = socket(PF_INET,SOCK_STREAM,0);
    int opt = 1;
    setsockopt(lisfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    evutil_make_socket_nonblocking(lisfd);

}


Tunnel::~Tunnel()
{
    delete laddr;
    delete raddr;
}

int Tunnel::init()
{
    if (bind(lisfd,laddr,socklen) < 0)
    {
        perror("bind");
        return -1;
    }
    if (listen(lisfd,20) < 0)
    {
        perror("listen");
        return -1;
    }
    return 0;
}

void Tunnel::run()
{
    Data *data = new Data(base,raddr,socklen);
    event *lisev = event_new(base,lisfd,EV_READ|EV_PERSIST,accept_callback,(void *)data);
    event_add(lisev,NULL);
    event_base_dispatch(base);
    delete data;
}

sockaddr* Tunnel::parse_addr(const std::string& addr)
{   
    int pos = addr.find(':');
    if (pos == std::string::npos || pos+3 > addr.length()) return NULL;
    int port = std::stoi(addr.substr(pos+1));
    sockaddr_in *sin = new sockaddr_in;
    memset(sin,0,sizeof(sin));
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    if (pos != 0 && inet_pton(AF_INET,addr.substr(0,pos).c_str(),&sin->sin_addr) < 0)
    {    
        delete sin;
        return NULL;
    }
    return (sockaddr*)sin;
}


void Tunnel::accept_callback(int lisfd, short events, void *ctx)
{
    Data *data = (Data *)ctx;

    int srcfd = accept(lisfd,NULL,NULL);
    if (srcfd < 0 && errno == EAGAIN)
    {
        errno = 0;
        perror("accept");
        return;
    }else if (srcfd < 0)
    {
        delete data;
        return;
    }
    evutil_make_socket_nonblocking(srcfd);

    int dstfd = socket(PF_INET,SOCK_STREAM,0);
    if (dstfd < 0) return;
    evutil_make_socket_nonblocking(dstfd);

    event *connev = event_new(data->base,dstfd,EV_WRITE|EV_TIMEOUT,Tunnel::connect_callback,(void*)data);
    data->srcfd = srcfd;
    data->dstfd = dstfd;
    data->connev = connev;

    int ret = connect(dstfd,data->raddr,data->socklen);
    if (ret == 0)
    {
        data->connected = true;
        Tunnel::connect_callback(dstfd,0,(void*)data);
        return;
    }else if (errno != EINPROGRESS)
    {
        close(srcfd);
        close(dstfd);
        delete data;
        return;
    }
    errno = 0;
    timeval timeout = {2,0};
    event_add(connev,&timeout);
}

void Tunnel::connect_callback(int dstfd, short events, void *ctx)
{
    Data *data = new Data(*(Data *)ctx);
    if (events & EV_TIMEOUT || !(events & EV_WRITE) && !data->connected)
    {
        close(data->srcfd);
        close(data->dstfd);
        delete data;
        return;
    }

    int err = 0;
    socklen_t errlen = sizeof(err);
    if (getsockopt(data->dstfd,SOL_SOCKET,SO_ERROR,&err,&errlen) < 0 || err != 0 )
    {
        close(data->srcfd);
        close(data->dstfd);
        delete data;
        return;
    }

    // pipe
    int forward[2], reverse[2];
    if (pipe(forward) < 0 || pipe(reverse) < 0)
    {
        close(data->srcfd);
        close(data->dstfd);
        delete data;
        return;
    }

    Buffer *fwd_buf = new Buffer(data->base,data->srcfd,data->dstfd,forward[0],forward[1]);
    Buffer *rev_buf = new Buffer(data->base,data->dstfd,data->srcfd,reverse[0],reverse[1]);

    event *fwd_read_ev = event_new(data->base,data->srcfd,EV_READ|EV_PERSIST,Tunnel::read_callback,(void*)fwd_buf);
    event *fwd_write_ev = event_new(data->base,data->srcfd,EV_WRITE|EV_PERSIST,Tunnel::write_callback,(void*)fwd_buf);
    event *rev_read_ev = event_new(data->base,data->dstfd,EV_READ|EV_PERSIST,Tunnel::read_callback,(void*)rev_buf);
    event *rev_write_ev = event_new(data->base,data->dstfd,EV_WRITE|EV_PERSIST,Tunnel::write_callback,(void*)rev_buf);
    fwd_buf->read_ev = fwd_read_ev; fwd_buf->write_ev = fwd_write_ev;
    rev_buf->read_ev = rev_read_ev; rev_buf->write_ev = rev_write_ev;
    
    event_add(fwd_read_ev,NULL);
    event_add(rev_read_ev,NULL);
    delete data;
}

void Tunnel::read_callback(int fd, short events, void *ctx)
{
    Buffer *buf = (Buffer *)ctx;
    int n;
    while(true)
    {
        n = splice(fd,NULL,buf->wpipe,NULL,Tunnel::blocksize,SPLICE_F_MORE|SPLICE_F_MOVE|SPLICE_F_NONBLOCK);
        if (n <= 0) break;
        event_add(buf->write_ev,NULL);
    }
    if (n < 0 && errno == EAGAIN)
    {
        errno = 0;
        return;
    }
    delete buf;
}

void Tunnel::write_callback(int fd, short events, void *ctx)
{
    Buffer *buf = (Buffer *)ctx;
    int n;
    while(true)
    {
        n = splice(buf->rpipe,NULL,buf->wfd,NULL,Tunnel::blocksize,SPLICE_F_MORE|SPLICE_F_MOVE|SPLICE_F_NONBLOCK);
        if (n <= 0) break;

    }
    if (n < 0 && errno == EAGAIN)
    {
        errno = 0;
        return;
    }
    event_del(buf->write_ev);
}
