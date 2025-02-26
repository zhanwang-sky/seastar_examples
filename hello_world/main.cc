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
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/when_all.hh>
#include <seastar/coroutine/maybe_yield.hh>

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

}

seastar::future<int> slow() {
  return seastar::sleep(std::chrono::seconds(3)).then([] { return 3; });
}

seastar::future<int> fast() {
  return seastar::make_ready_future<int>(0);
}

seastar::future<int> slow_accum(int n) {
  int sum = 0;
  for (int i = 1; i <= n; ++i) {
    sum += i;
    co_await seastar::coroutine::maybe_yield();
  }
  co_return sum;
}

seastar::future<> lambda_coroutine_wrapper() {
  co_await seastar::sleep(std::chrono::nanoseconds(1)).then(seastar::coroutine::lambda([]() -> seastar::future<> {
    co_await seastar::coroutine::maybe_yield();
    cout << "Hello from lambda coroutine\n";
    co_return;
  }));
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
    app.run(argc, argv, lifetime_management::f);
  } catch (...) {
    cerr << "Exception caught: " << std::current_exception() << endl;
    return 1;
  }

  return 0;
}
