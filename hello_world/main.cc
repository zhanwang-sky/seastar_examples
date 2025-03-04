//
//  hello_world/main.cc
//  seastar_examples
//
//  Created by zhanwang-sky on 2025/2/18.
//

// c++ main.cc $(pkg-config --libs --cflags --static seastar) -o hello_world

#include <iostream>

#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/seastar.hh>
#include <seastar/core/shard_id.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/when_all.hh>
#include <seastar/coroutine/maybe_yield.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>

using std::cout;
using std::cerr;
using std::endl;

class MyClass {
 public:
  MyClass(const void* arg = nullptr) {
    printf("[shard %u] MyClass - construct: %p with {%p}\n", seastar::this_shard_id(), this, arg);
  }

  MyClass(const MyClass& orig) {
    printf("[shard %u] MyClass - copy construct: %p = %p\n", seastar::this_shard_id(), this, &orig);
  }

  MyClass(MyClass&& orig) {
    printf("[shard %u] MyClass - move construct: %p <= %p\n", seastar::this_shard_id(), this, &orig);
  }

  MyClass(const MyClass&& orig) {
    printf("[shard %u] MyClass - FAKE construct: %p X= %p\n", seastar::this_shard_id(), this, &orig);
  }

  MyClass& operator=(const MyClass& rhs) noexcept {
    printf("[shard %u] MyClass - copy asign: %p = %p\n", seastar::this_shard_id(), this, &rhs);
    return *this;
  }

  MyClass& operator=(MyClass&& rhs) noexcept {
    printf("[shard %u] MyClass - move asign: %p <= %p\n", seastar::this_shard_id(), this, &rhs);
    return *this;
  }

  virtual ~MyClass() {
    printf("[shard %u] MyClass - destruct: %p\n", seastar::this_shard_id(), this);
  }

  void operator()() && {
    printf("[shard %u] MyClass - call on rvalue: %p\n", seastar::this_shard_id(), this);
  }

  void operator()() const& {
    printf("[shard %u] MyClass - call on const lvalue: %p\n", seastar::this_shard_id(), this);
  }

  void operator()() const&& {
    printf("[shard %u] MyClass - call on CONST rvalue: %p\n", seastar::this_shard_id(), this);
  }
};

// Lifetime management

namespace lifetime_management {

using namespace std::chrono_literals;
using namespace seastar;

template <typename T>
future<> slow_op(T& op) {
  cout << "slow_op: Scheduling op(" << &op << ") in 1 second\n";

  return sleep(1s).then([&op]() {
    cout << "slow_op: Executing op(" << &op << ") on lvalue:\n";
    op();
  }).then([&op]() {
    cout << "slow_op: Executing op(" << &op << ") on rvalue:\n";
    std::move(op)();
  });
}

future<> f() {
  auto task_1_done = make_lw_shared<bool>(false);
  return when_all(
    do_with(MyClass(), [task_1_done](auto& op) {
              *task_1_done = true;
              return slow_op(op);
            }),
    do_with(MyClass(), slow_op<MyClass>)
  ).discard_result().then([task_1_done]() {
    cout << "all done, task_1_done=" << *task_1_done << endl;
  });
}

} // lifetime_management

// Coroutines

namespace coroutines {

using namespace std::chrono_literals;
using namespace seastar;

future<int> slow_accum(int n) {
  // sanity check
  if (n < 0) {
    // This may impact performance, not recommended.
    // throw std::invalid_argument("call sloc_accum with negative value");
    co_return coroutine::exception(std::make_exception_ptr(std::invalid_argument("call sloc_accum with negative value")));
  }

  int sum = 0;
  for (int i = 0; i <= n; ++i) {
    sum += i;
    co_await coroutine::maybe_yield();
  }
  co_return sum;
}

future<> f() {
  MyClass obj;

  try {
    int sum;

    cout << "accumulate from 1 to 100...\n";
    sum = co_await slow_accum(100);
    cout << "sum=" << sum << endl;

    cout << "accumulate -1?\n";
    // do not yield
    sum = co_await coroutine::without_preemption_check(slow_accum(-1));
    cout << "sum=" << sum << endl;

  } catch (...) {
    cerr << "exception caught: " << std::current_exception() << endl;
  }

  // Notes:
  // seastar::future::then() accepts a continuation
  // We wrap the argument to seastar::future::then() with seastar::coroutine::lambda()
  // We ensure evaluation of the lambda completes within the same expression using the outer co_await.
  cout << "scheduling a lambda coroutine with obj " << &obj << " in 1s\n";
  co_await sleep(1s).then(coroutine::lambda([&obj]() -> future<> { // with outer co_await, we can safely capture obj by reference
    cout << "in lambda coroutine, scheduling slow_op on obj " << &obj << " in 500ms\n";
    co_await sleep(1s); // make lambda a coroutine, should be enclosed by coroutine::lambda()
    obj();
  }));

  // check
  co_await sleep(1s).then([&obj]() {
    cout << "at this point, obj " << &obj << " is still available\n";
  });
}

} // coroutines

// Network stack

namespace network {

using namespace std::chrono_literals;
using namespace seastar;

logger log("network");

future<> do_echo(accept_result&& res) {
  auto& s = res.connection;
  auto in = s.input();
  auto out = s.output();
  size_t bytes_transferred = 0;

  while (true) {
    auto tmp_buf = co_await in.read();
    if (!tmp_buf) {
      log.info("{} disconnected, {} bytes transferred",
               res.remote_address, bytes_transferred);
      co_return;
    }
    bytes_transferred += tmp_buf.size();
    co_await out.write(std::move(tmp_buf));
    co_await out.flush();
  }
}

future<> service_loop() {
  listen_options opts = {
    .reuse_address = true,
    .lba = server_socket::load_balancing_algorithm::port
  };
  auto sock = listen(seastar::make_ipv4_address({8880}), opts);
  log.info("Listening on {} ...", sock.local_address());
  while (true) {
    auto res = co_await sock.accept();
    log.info("Accepted connection from {}", res.remote_address);
    static_cast<void>(do_echo(std::move(res)));
  }
}

future<> f() {
  return parallel_for_each(std::views::iota(0u, smp::count),
                           [](unsigned c) { return smp::submit_to(c, service_loop); });
}

} // network

namespace sharded_services {

using namespace seastar;

struct MyService : public MyClass {
  MyService(const MyClass& obj) : MyClass(&obj) { }

  virtual ~MyService() { }

  future<> run() {
    (*this)();
    return make_ready_future<>();
  }

  future<> stop() {
    return make_ready_future<>();
  }
};

future<> f() {
  sharded<MyService> s;
  co_await s.start(MyClass()).then([&s]() { // MyClass copy many many times...
    return s.invoke_on(1u, [](MyService& service) {
      return service.run();
    });
  }).then([&s]() {
    return s.stop();
  });
}

} // sharded_services

seastar::future<int> slow() {
  return seastar::sleep(std::chrono::seconds(3)).then([] { return 3; });
}

seastar::future<int> fast() {
  return seastar::make_ready_future<int>(0);
}

seastar::future<> pass() {
  cout << "will pass\n";
  return seastar::make_ready_future<>();
}

seastar::future<> fail() {
  cout << "will fail\n";
  return seastar::make_exception_future<>(std::runtime_error("fail"));
}

seastar::future<> fail_throw() {
  cout << "will throw\n";
  throw std::runtime_error("fail_throw");
}

seastar::future<> f() {
  return fail_throw().then([] {
    cout << "fail_throw done? invoke fail()\n";
    return fail();
  }).then([] {
    cout << "fail done? invoke pass()\n";
    return pass();
  }).then([] {
    cout << "passed\n";
  }).finally([] {
    cout << "finally get here\n";
  });
}

int main(int argc, char* argv[]) {
  seastar::app_template app;

  try {
    app.run(argc, argv, sharded_services::f);
  } catch (...) {
    cerr << "Exception caught: " << std::current_exception() << endl;
    return 1;
  }

  return 0;
}
