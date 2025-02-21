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

seastar::future<> f() {
  auto fast_val = fast().then([](int val) {
    cout << "fast done, val=" << val << endl;
  });

  auto slow_val = slow().then([](int val) {
    cout << "slow done, val=" << val << endl;
  });

  auto slow_sum = slow_accum(100).then([](int sum) {
    cout << "slow_accum(100) done, sum=" << sum << endl;
  });

  return when_all(std::move(fast_val),
                  std::move(slow_val),
                  std::move(slow_sum),
                  lambda_coroutine_wrapper()).discard_result(); // convert to future<>
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
