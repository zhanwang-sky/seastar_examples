//
//  main.cc
//  seastar_examples
//
//  Created by zhanwang-sky on 2025/3/21.
//

// c++ main.cc $(pkg-config --libs --cflags --static seastar)

#include <cstdio>
#include <chrono>
#include <iostream>
#include <string>
#include <utility>

#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/when_all.hh>
#include <seastar/util/log.hh>

using namespace std::chrono_literals;

seastar::logger logger("examples");

seastar::future<> futures_n_continuations() {
  // future的状态
  auto fut_ready = seastar::make_ready_future<>();
  auto fut_slow = seastar::sleep(1s);
  logger.info("is fut_ready available? {}", fut_ready.available());
  logger.info("is fut_slow available? {}", fut_slow.available());

  // future的延续及类型
  //                                 future<int>                   future<double>
  auto fut_2pi = fut_ready.then([] { return 2; }).then([](int x) { return x * 3.14; });
  logger.info("fut_2pi has type future<double>");

  // future的结果
  // 1. 通过延续获取（并传递结果）
  auto fut_2pi_2 = fut_2pi.then([](double val) {
    logger.info("fut_2pi has value {}", val);
    return val;
  });
  // 2. 通过get()获取，需先检查状态
  if (fut_2pi_2.available()) {
    logger.info("fut_2pi_2 is available and has value {}", fut_2pi_2.get());
  } else {
    logger.warn("fut_2pi_2 is not available!");
  }
  // future只能消费一次
  // fut_2pi_2.get();
  // fut_2pi_2.get();
  // fut_2pi_2.get();
  // core dumped

  // 函数返回时，fut_slow仍未执行完，消息会在函数返回后打印
  return fut_slow.then([] { logger.info("fut_slow done"); });
}

seastar::future<> exceptional_futures() {
  // 1. 对于ready_future，可以直接用failed()判断
  auto fail_now = seastar::make_exception_future<int>(std::runtime_error("fail now"));
  if (fail_now.available()) {
    if (fail_now.failed()) {
      logger.warn("fail_now has failed");
      // 消费fail_now的异常状态，否则相当于没接住异常，报warning
      fail_now.ignore_ready_future(); // 只能在available状态的future上调用
    } else {
      logger.info("fail_now has value {}", fail_now.get());
    }
  }
  // 也可以在try/cacth中调用get()
  auto fail_now_2 = seastar::make_exception_future<unsigned>(std::runtime_error("fail now 2"));
  if (fail_now_2.available()) {
    try {
      auto res = fail_now_2.get();
      logger.info("fail_now_2 has value {}", res);
    } catch (...) {
      logger.warn("fail_now_2 has exception {}", std::current_exception());
    }
  }

  // 2. 在延续中捕获异常
  auto fail_later = seastar::sleep(500ms).then([] {
    return seastar::make_exception_future<double>(std::runtime_error("fail later"));
  });
  // fail_later has type future<double>

  // then()只能接收结果，不能捕获异常
  auto fail_later_2 = fail_later.then([](double val) {
    logger.info("fail_later has value {}", val);
    return std::to_string(val);
  });
  // fail_later_2 has type future<std::string>

  // handle_exception()可以捕获异常，甚至可以重新抛出（传递）异常
  auto fail_later_3 = fail_later_2.handle_exception([](std::exception_ptr ep) {
    logger.warn("fail_later_2 has exception {}, rethrow it", ep);
    return seastar::make_exception_future<std::string>(ep);
  });
  // fail_later_3 has the same type as fail_later_2

  // 使用then_wrapped()捕获可能包含异常的future
  auto fail_later_4 = fail_later_3.then_wrapped([](seastar::future<std::string> fut) {
    // 此处拿到的future肯定是ready的
    if (fut.failed()) {
      logger.warn("fail_later_3 has failed");
      fut.ignore_ready_future(); // 消费异常状态，否则会报错
      return "exception handled";
    }
    auto msg = fut.get();
    logger.info("fail_later_3 has value {}", msg);
    return "future consumed";
  });
  // fail_later_4 has type future<const char*>

  // 可以在最后用finally()安装一个延续，无论是否抛异常都会调用（但它不会消费异常结果）
  auto fail_finally = fail_later_4.finally([] {
    logger.info("whether it succeeds or not, we'll finally get here");
  });
  // fail_finally has the same type as fail_later_4

  return fail_finally.then([](const char* msg) {
    logger.info("finally, we got \"{}\"", msg);
  });
}

seastar::future<> throwing_exceptions() {
  // 会抛出异常的函数
  auto sanity_check = [](int arg) -> int {
    if (!arg) {
      throw std::invalid_argument("INVALID ARGUMENT '0'!");
    }
    return arg;
  };

  // 将可能抛异常的函数放在futurize_invoke()中执行，会将异常封装成exception_future
  auto maybe_exceptional = seastar::futurize_invoke(sanity_check, 0).then_wrapped([](seastar::future<int> f) {
    if (f.failed()) {
      logger.warn("throwing_exceptions: sanity check failed, throw runtime_error!");
      f.ignore_ready_future();
      // 在延续中抛异常，也会被封装成exception_future
      throw std::runtime_error("sanity check failed");
    }
    auto val = f.get();
    logger.info("throwing_exceptions: sanity check passed, got {}", val);
    return val;
  }).finally([] {
    logger.info("throwing_exceptions: finally, do some cleanup...");
  });

  // discard_result()丢弃了future的类型，handle_exception()的回调函数直接返回void即可
  return maybe_exceptional.discard_result().handle_exception([](std::exception_ptr ep) {
    logger.warn("throwing_exceptions: got exception {}", ep);
  });
}

seastar::future<> introduce_future() {
  auto ret = seastar::when_all(futures_n_continuations(),
                               exceptional_futures(),
                               throwing_exceptions());
  return ret.discard_result().then([] { logger.info("all done"); });
}

int main(int argc, char* argv[]) {
  seastar::app_template app;

  try {
    app.run(argc, argv, introduce_future);
  } catch (...) {
    logger.error("Exception caught: {}", std::current_exception());
    return 1;
  }

  return 0;
}
