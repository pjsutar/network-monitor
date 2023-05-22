#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>

#include <iomanip>
#include <iostream>
#include <thread>

using tcp = boost::asio::ip::tcp;

void Log(boost::system::error_code ec)
{
    std::cerr << "[" << std::setw(14) << std::this_thread::get_id() << "] "
        << (ec ? "Error: " : "OK")
        << (ec ? ec.message() : "")
        << std::endl;
}

void OnConnect(boost::system::error_code ec)
{
    Log(ec);
}

int main()
{
    std::cerr << "[" << std::setw(14) << std::this_thread::get_id() << "] main"
        << std::endl;

    // Always start with an I/O context object.
    boost::asio::io_context ioc{};

    // Create an I/O object. Every Boost.Asio I/O object API needs an io_context
    // as the first parameter.
    tcp::socket socket{ boost::asio::make_strand(ioc) };

    size_t nThreads{ 4 };

    // Under the hood, socket.connect uses I/O context to talk to the socket
    // and get a response back. The response is saved in ec.
    boost::system::error_code ec{};
    tcp::resolver resolver{ ioc };
    auto resolverIt{ resolver.resolve("google.com", "80", ec) };
    if (ec) {
        Log(ec);
        return -1;
    }
    for (size_t idx{ 0 }; idx < nThreads; ++idx) {
        socket.async_connect(*resolverIt, OnConnect);
    }

    // We must call io_context::run for asynchronous callbacks to run.
    std::vector<std::thread> threads{};
    threads.reserve(nThreads);
    for (size_t idx{ 0 }; idx < nThreads; ++idx) {
        threads.emplace_back([&ioc]() {
            ioc.run();
            });
    }
    for (size_t idx{ 0 }; idx < nThreads; ++idx) {
        threads[idx].join();
    }

    return 0;
}