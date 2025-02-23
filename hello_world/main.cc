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
#include <seastar/core/sleep.hh>
#include <seastar/core/when_all.hh>
#include <seastar/coroutine/maybe_yield.hh>

using std::cout;
using std::cerr;
using std::endl;

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
    app.run(argc, argv, f);
  } catch (...) {
    cerr << "Exception caught: " << std::current_exception() << endl;
    return 1;
  }

  return 0;
}
