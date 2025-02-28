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
#include <seastar/core/shard_id.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/when_all.hh>
#include <seastar/coroutine/maybe_yield.hh>
#include <seastar/util/log.hh>

using std::cout;
using std::cerr;
using std::endl;

class MyClass {
 public:
  MyClass() {
    cout << "construct: " << this << endl;
  }

  MyClass(const MyClass& orig) {
    cout << "copy construct: " << this << " = " << &orig << endl;
  }

  MyClass(MyClass&& orig) noexcept {
    cout << "move construct: " << this << " <= " << &orig << endl;
  }

  MyClass(const MyClass&& orig) {
    cout << "FAKE construct: " << this << " X= " << &orig << endl;
  }

  MyClass& operator=(const MyClass& orig) {
    cout << "copy assign: " << this << " = " << &orig << endl;
    return *this;
  }

  MyClass& operator=(MyClass&& orig) noexcept {
    cout << "move assign: " << this << " <= " << &orig << endl;
    return *this;
  }

  virtual ~MyClass() {
    cout << "destruct: " << this << endl;
  }

  void operator()() && {
    cout << "call on rvalue: " << this << endl;
  }

  void operator()() const& {
    cout << "call on lvalue: " << this << endl;
  }

  void operator()() const&& {
    cout << "call on CONST rvalue: " << this << endl;
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

using namespace seastar;

logger log("network");

future<> service_loop() {
  log.info("Hello from core {}", this_shard_id());
  return make_ready_future<>();
}

future<> f() {
  return parallel_for_each(std::views::iota(0u, smp::count),
                           [](unsigned c) {
                             return smp::submit_to(c, service_loop);
                           });
}

} // network

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
    app.run(argc, argv, network::f);
  } catch (...) {
    cerr << "Exception caught: " << std::current_exception() << endl;
    return 1;
  }

  return 0;
}
