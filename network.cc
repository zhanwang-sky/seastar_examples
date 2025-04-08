//
//  network.cc
//  seastar_examples
//
//  Created by zhanwang-sky on 2025/4/8.
//

// c++ network.cc $(pkg-config --libs --cflags --static seastar)

#include <seastar/core/app-template.hh>
#include <seastar/core/future.hh>
#include <seastar/util/log.hh>

seastar::logger logger("network");

int main(int argc, char* argv[]) {
  seastar::app_template app;

  try {
    app.run(argc, argv, [] {
      logger.info("network");
      return seastar::make_ready_future<>();
    });
  } catch (...) {
    logger.error("Exception caught: {}", std::current_exception());
    return 1;
  }

  return 0;
}
