//
//  hello_world/main.cc
//  seastar_examples
//
//  Created by zhanwang-sky on 2025/2/18.
//

// g++ main.cc $(pkg-config --libs --cflags --static seastar) -o hello_world

#include <iostream>

#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>

int main(int argc, char* argv[]) {
  seastar::app_template app;

  app.run(argc, argv, []() {
    std::cout << "Hello world\n";
    return seastar::make_ready_future<>();
  });

  return 0;
}
