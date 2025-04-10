//
//  network.cc
//  seastar_examples
//
//  Created by zhanwang-sky on 2025/4/8.
//

// c++ network.cc $(pkg-config --libs --cflags --static seastar)

#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/when_all.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>

seastar::logger logger("network");

seastar::future<> tcp_server(uint16_t port) {
  seastar::listen_options lo;
  lo.reuse_address = true;

  auto sock = seastar::listen(seastar::make_ipv4_address({port}), lo);

  auto do_echo = [](seastar::connected_socket&& conn) -> seastar::future<> {
    auto in = conn.input();
    auto out = conn.output();
    for (;;) {
      auto buf = co_await in.read();
      if (!buf) {
        break;
      }
      co_await out.write(std::move(buf));
      co_await out.flush();
    }
  };

  for (;;) {
    auto res = co_await sock.accept();
    (void) do_echo(std::move(res.connection));
  }
}

seastar::future<> udp_server(uint16_t port) {
  auto chan = seastar::make_bound_datagram_channel(seastar::make_ipv4_address({port}));

  for (;;) {
    auto dgram = co_await chan.receive();
    co_await chan.send(dgram.get_src(), std::move(dgram.get_data()));
  }
}

seastar::future<> introduce_network_stack() {
  return seastar::when_all(tcp_server(1234), udp_server(1234)).discard_result();
}

int main(int argc, char* argv[]) {
  seastar::app_template app;

  try {
    app.run(argc, argv, introduce_network_stack);
  } catch (...) {
    logger.error("Exception caught: {}", std::current_exception());
    return 1;
  }

  return 0;
}
