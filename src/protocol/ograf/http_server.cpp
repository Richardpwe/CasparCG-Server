/*
 * Copyright (c) 2026 CasparCG contributors
 *
 * This file is part of CasparCG (www.casparcg.com).
 *
 * CasparCG is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "http_server.h"

#include "router.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <stdexcept>
#include <thread>
#include <utility>

namespace caspar::protocol::ograf {

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
using tcp       = asio::ip::tcp;

namespace {

constexpr std::uint64_t max_request_body_bytes = 4U * 1024U * 1024U;

class http_session final : public std::enable_shared_from_this<http_session>
{
  public:
    http_session(tcp::socket socket, router& request_router, const bool allow_cors)
        : stream_(std::move(socket))
        , router_(request_router)
        , allow_cors_(allow_cors)
    {
        parser_.body_limit(max_request_body_bytes);
    }

    void run()
    {
        http::async_read(stream_,
                         buffer_,
                         parser_,
                         [self = shared_from_this()](const boost::system::error_code& error, const std::size_t) {
                             self->on_read(error);
                         });
    }

  private:
    void on_read(const boost::system::error_code& error)
    {
        if (error == http::error::body_limit) {
            const auto& request = parser_.get();
            send_response(
                make_problem_response(
                    413,
                    "Request body exceeds the 4194304 byte limit",
                    std::string(request.target())),
                request.version() == 0 ? 11 : request.version());
            return;
        }
        if (error) {
            return;
        }

        auto       request    = parser_.release();
        const auto api_result = router_.route(
            {std::string(request.method_string()), std::string(request.target()), std::move(request.body())});

        send_response(api_result, request.version());
    }

    void send_response(const api_response& api_result, const unsigned int version)
    {
        response_.version(version);
        response_.result(static_cast<http::status>(api_result.status));
        response_.keep_alive(false);
        response_.set(http::field::server, "CasparCG OGraf");
        if (!api_result.content_type.empty()) {
            response_.set(http::field::content_type, api_result.content_type);
        }
        if (allow_cors_) {
            response_.set(http::field::access_control_allow_origin, "*");
            response_.set(http::field::access_control_allow_headers, "Content-Type");
            response_.set(http::field::access_control_allow_methods, "GET, POST, PUT, DELETE, OPTIONS");
        }
        response_.body() = api_result.body;
        response_.prepare_payload();

        http::async_write(
            stream_, response_, [self = shared_from_this()](const boost::system::error_code&, const std::size_t) {
                boost::system::error_code ignored;
                self->stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
            });
    }

    beast::tcp_stream                       stream_;
    beast::flat_buffer                      buffer_;
    http::request_parser<http::string_body> parser_;
    http::response<http::string_body>       response_;
    router&                                 router_;
    bool                                    allow_cors_;
};

asio::ip::address listener_address(const std::string& host)
{
    if (host == "localhost") {
        return asio::ip::make_address("127.0.0.1");
    }

    boost::system::error_code error;
    auto                      address = asio::ip::make_address(host, error);
    if (error) {
        throw std::invalid_argument("Invalid OGraf server host '" + host + "': " + error.message());
    }
    return address;
}

} // namespace

class http_server::impl
{
  public:
    impl(router& request_router, http_server_config config)
        : router_(request_router)
        , address_(listener_address(config.host))
        , acceptor_(context_)
    {
        boost::system::error_code error;
        const tcp::endpoint       endpoint(address_, config.port);

        acceptor_.open(endpoint.protocol(), error);
        throw_on_error(error, "open");
        acceptor_.set_option(asio::socket_base::reuse_address(true), error);
        throw_on_error(error, "set reuse address");
        acceptor_.bind(endpoint, error);
        throw_on_error(error, "bind");
        acceptor_.listen(asio::socket_base::max_listen_connections, error);
        throw_on_error(error, "listen");

        port_ = acceptor_.local_endpoint().port();
        accept();
        worker_ = std::thread([this] { context_.run(); });
    }

    ~impl()
    {
        asio::post(context_, [this] {
            boost::system::error_code ignored;
            acceptor_.cancel(ignored);
            acceptor_.close(ignored);
        });
        context_.stop();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    std::uint16_t port() const noexcept { return port_; }
    bool          is_local() const noexcept { return address_.is_loopback(); }

  private:
    static void throw_on_error(const boost::system::error_code& error, const char* operation)
    {
        if (error) {
            throw std::runtime_error(std::string("Failed to ") + operation +
                                     " OGraf HTTP listener: " + error.message());
        }
    }

    void accept()
    {
        acceptor_.async_accept([this](const boost::system::error_code& error, tcp::socket socket) {
            if (!error) {
                std::make_shared<http_session>(std::move(socket), router_, address_.is_loopback())->run();
            }
            if (acceptor_.is_open()) {
                accept();
            }
        });
    }

    router&           router_;
    asio::io_context  context_{1};
    asio::ip::address address_;
    tcp::acceptor     acceptor_;
    std::thread       worker_;
    std::uint16_t     port_{};
};

http_server::http_server(router& request_router, http_server_config config)
    : impl_(std::make_unique<impl>(request_router, std::move(config)))
{
}

http_server::~http_server() = default;

std::uint16_t http_server::port() const noexcept { return impl_->port(); }
bool          http_server::is_local() const noexcept { return impl_->is_local(); }

} // namespace caspar::protocol::ograf
