**创建`datagram_channel`**
```
datagram_channel make_bound_datagram_channel(const socket_address& local) // seastar.hh
  engine().net().make_bound_datagram_channel(local) // reactor.cc
  // reactor.network_stack::make_bound_datagram_channel()
  // class posix_network_stack : public network_stack { ... }
  // datagram_channel posix_network_stack::make_bound_datagram_channel(const socket_address& local)
    datagram_channel(std::make_unique<posix_datagram_channel>(local)) // posix-stack.cc
    // posix_datagram_channel::posix_datagram_channel(socket_address local)
      auto fd = create_socket(local.family()) // posix-stack.cc
      // static file_desc posix_datagram_channel::create_socket(sa_family_t family)
        file_desc fd = file_desc::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0) // posix-stack.cc
        // static file_desc file_desc::socket(int family, int type, int protocol = 0)
          ::socket(family, type, protocol) // posix.hh
        fd.setsockopt(SOL_IP, IP_PKTINFO, true)
        fd.setsockopt(SOL_SOCKET, SO_REUSEPORT, 1)
      fd.bind(local.u.sa, local.addr_length)
```

**接收`datagram`**
```
future<datagram> datagram_channel::receive() // api.hh
  _impl->receive() // stack.cc
  // datagram_channel.datagram_channel_impl::receive()
  // class posix_datagram_channel : public datagram_channel_impl { ... }
  // future<datagram> posix_datagram_channel::receive()
    _fd.recvmsg(&_recv._hdr) // posix-stack.cc
    // posix_datagram_channel.pollable_fd::recvmsg()
    // future<size_t> pollable_fd::recvmsg(struct msghdr *msg)
      _s->recvmsg(msg) // pollable_fd.hh
      // pollable_fd.pollable_fd_state::recvmsg()
      // future<size_t> pollable_fd_state::recvmsg(struct msghdr *msg)
        engine().readable(*this) // reactor.cc
        // reactor::readable()
        // future<> reactor::readable(pollable_fd_state& fd)
          _backend->readable(fd) // reactor.cc
          // reactor.reactor_backend::readable()
          // class reactor_backend_epoll : public reactor_backend { ... }
          // future<> reactor_backend_epoll::readable(pollable_fd_state& fd)
            get_epoll_future(fd, EPOLLIN) // reactor_backend.cc
            // future<> reactor_backend_epoll::get_epoll_future(pollable_fd_state& pfd, int event)
              ::epoll_ctl(_epollfd.get(), ctl, pfd.fd.get(), &eevt) // reactor_backend.cc
```
