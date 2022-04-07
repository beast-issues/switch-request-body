#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _SCL_SECURE_NO_WARNINGS
#define _WIN32_WINDOWS 0x0601

#include <SDKDDKVer.h>
#include <direct.h>
#else
#include <sys/resource.h>
#include <sys/time.h>

#include <unistd.h>
#endif

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <iostream>

namespace beast = boost::beast;
namespace asio  = boost::asio;
using tcp       = asio::ip::tcp;

auto
detach_note_error(std::string name)
{
    return [name](std::exception_ptr ep)
    {
        if (ep)
        {
            try
            {
                std::rethrow_exception(ep);
            }
            catch (std::exception &e)
            {
                std::cout << name << " : exception : " << e.what() << std::endl;
                return;
            }
        }
        std::cout << name << " : complete" << std::endl;
    };
}

asio::awaitable< void >
process_request(tcp::socket sock)
{
    using asio::use_awaitable;

    auto buf = beast::flat_buffer();

    // collect the header

    auto header_parser =
        beast::http::request_parser< beast::http::empty_body >();
    co_await beast::http::async_read_header(
        sock, buf, header_parser, use_awaitable);

    {
        auto &request = header_parser.get();
        std::cout << "process_request : header :\n" << request << std::endl;
    }

    // collect the rest of the request

    auto body_parser = beast::http::request_parser< beast::http::string_body >(
        std::move(header_parser));

    co_await beast::http::async_read(sock, buf, body_parser, use_awaitable);

    // exception here: bad version [beast.http:14] because body_parser is state
    // is still the initial state

    std::cout << "process_request : body : " << body_parser.get().body()
              << std::endl;

    auto response = beast::http::response<beast::http::string_body>();
    response.keep_alive(false);
    response.body() = body_parser.get().body();
    co_await beast::http::async_write(sock, response, use_awaitable);
}

asio::awaitable< void >
server(tcp::acceptor &acceptor)
{
    using asio::use_awaitable;

    auto exec = co_await asio::this_coro::executor;

    while (1)
    {
        auto sock = tcp::socket(exec);
        co_await acceptor.async_accept(sock, use_awaitable);

        auto ctx = [&]
        {
            std::stringstream ss;
            ss << "process_request(" << sock.local_endpoint() << ")";
            return ss.str();
        }();

        co_spawn(
            exec, process_request(std::move(sock)), detach_note_error(ctx));

        // todo: remove
        break;
    }
}

asio::awaitable< void >
client(tcp::endpoint server_endpoint)
{
    using asio::use_awaitable;

    auto exec = co_await asio::this_coro::executor;

    auto sock = tcp::socket(exec);
    co_await sock.async_connect(server_endpoint, use_awaitable);

    auto request = beast::http::request< beast::http::string_body >();
    request.base().version(11);
    request.base().method(beast::http::verb::post);
    request.base().target("/foo");
    request.body() = "The cat sat on the mat";
    request.prepare_payload();
    co_await beast::http::async_write(sock, request, use_awaitable);

    auto response = beast::http::response< beast::http::string_body >();
    auto buf      = beast::flat_buffer();
    co_await beast::http::async_read(sock, buf, response, use_awaitable);

    std::cout << "client:\n" << response << std::endl;
}

int
main()
{
    auto ioc = asio::io_context();

    auto listen_ep = tcp::endpoint(asio::ip::address_v4::loopback(), 0);
    auto acceptor  = tcp::acceptor(ioc, listen_ep);
    acceptor.listen();

    // run the web server
    asio::co_spawn(ioc, server(acceptor), detach_note_error("server"));

    // run the web client
    asio::co_spawn(
        ioc, client(acceptor.local_endpoint()), detach_note_error("client"));

    // pump messages until all coroutines are complete.
    ioc.run();

    return 0;
}