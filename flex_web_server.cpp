//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: Advanced server, flex (plain + SSL)
//
//------------------------------------------------------------------------------

#include "beast.h"
#include "server_certificate.hpp"

#include "logger/logger.h"
#include <algorithm>
#include <atomic>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/config.hpp>
#include <boost/make_unique.hpp>
#include <boost/optional.hpp>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;          // from <boost/beast.hpp>
namespace http = beast::http;            // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;  // from <boost/beast/websocket.hpp>
namespace net = boost::asio;             // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;        // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp;        // from <boost/asio/ip/tcp.hpp>

extern beast::string_view mime_type(beast::string_view path);
extern std::string path_cat(beast::string_view base, beast::string_view path);
extern void fail(beast::error_code ec, char const* what);

// CORS headers applied to all responses (PWA/browser compatibility)
namespace {
    constexpr auto CORS_ALLOW_HEADERS = "Origin, X-Requested-With, Content-Type, Accept, Authorization, Access-Control-Allow-Origin, X-Session-Key";
    constexpr auto CORS_ALLOW_METHODS = "POST, GET, PUT, DELETE, PATCH, HEAD, OPTIONS";
    constexpr auto CORS_ALLOW_ORIGIN = "*";
    constexpr auto CORS_ALLOW_CREDENTIALS = "true";

    // Helper to apply common CORS + server headers to any response
    template<class Res>
    void apply_common_headers(Res& res) {
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::access_control_allow_headers, CORS_ALLOW_HEADERS);
        res.set(http::field::access_control_allow_methods, CORS_ALLOW_METHODS);
        res.set(http::field::access_control_allow_origin, CORS_ALLOW_ORIGIN);
        res.set(http::field::access_control_allow_credentials, CORS_ALLOW_CREDENTIALS);
    }
}

// Return a response for the given request.
//
// The concrete type of the response message (which depends on the
// request), is type-erased in message_generator.
template<class Body, class Allocator>
http::message_generator handle_request(
    beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req) {
    // Returns a bad request response
    auto const bad_request =
        [&req](beast::string_view why) {
            http::response<http::string_body> res {http::status::bad_request, req.version()};
            apply_common_headers(res);
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            res.body() = R"({"success":false,"error":{"code":"BAD_REQUEST","message":")" + std::string(why) + R"("}})";
            res.prepare_payload();
            return res;
        };

    // Returns a not found response
    auto const not_found =
        [&req](beast::string_view target) {
            http::response<http::string_body> res {http::status::not_found, req.version()};
            apply_common_headers(res);
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            res.body() = R"({"success":false,"error":{"code":"NOT_FOUND","message":"Resource not found"}})";
            res.prepare_payload();
            return res;
        };

    // Returns a server error response
    auto const server_error =
        [&req](beast::string_view what) {
            http::response<http::string_body> res {http::status::internal_server_error, req.version()};
            apply_common_headers(res);
            res.set(http::field::content_type, "application/json");
            res.keep_alive(req.keep_alive());
            res.body() = R"({"success":false,"error":{"code":"INTERNAL_ERROR","message":")" + std::string(what) + R"("}})";
            res.prepare_payload();
            return res;
        };

    std::string _path = req.target();
#ifdef _DEBUG
    LOG_INFO("Target: {}", _path);
#endif
    uint64_t size = 1;
    auto const head_response =
        [&req, &size, &_path]() {
            http::response<http::empty_body> res {http::status::ok, req.version()};
            apply_common_headers(res);
            res.set(http::field::content_type, mime_type(_path));
            res.content_length(size);
            res.keep_alive(req.keep_alive());
            return res;
        };

    if (req.method() == http::verb::head) {
        return head_response();
    }

    auto const options_response =
        [&req, &size, &_path]() {
            http::response<http::empty_body> res {http::status::ok, req.version()};
            apply_common_headers(res);

            auto originHeader = req.find(boost::beast::http::field::origin);
            if (originHeader != req.end()) {
                std::string origin(originHeader->value().data(), originHeader->value().size());
                res.set(http::field::access_control_allow_origin, origin);
            }
            res.content_length(size);
            res.keep_alive(req.keep_alive());
            return res;
        };

    if (req.method() == http::verb::options) {
#ifdef _DEBUG
        LOG_INFO("OPTION requested");
#endif
        return options_response();
    }
    // Make sure we can handle the method
    if (req.method() == http::verb::get
        || req.method() == http::verb::post
        || req.method() == http::verb::put
        || req.method() == http::verb::delete_) {
        ;  //
    } else {
        LOG_ERROR("{} failed!!!", boost::beast::http::to_string(req.method()));
        return bad_request("Unknown HTTP-method");
    }

    // Request path must be absolute and not contain "..".
    if (req.target().empty() || req.target()[0] != '/' || req.target().find("..") != beast::string_view::npos)
        return bad_request("Illegal request-target");

    // Build the path to the requested file
    // auto method = req.method();
    // auto cmd = req.target();
    // auto body = req.body();

    auto [type, path, binarybuf] = process_web_command(req.method(), req.target(), req.body(), [&req](boost::beast::string_view key) -> boost::beast::string_view {
        return req[key];
    });

    if (type == 1) {
        http::response<http::string_body> res {http::status::ok, req.version()};
        apply_common_headers(res);
        // Auto-detect JSON responses (start with { or [)
        bool isJson = (!path.empty() && (path[0] == '{' || path[0] == '['));

        // Detect image content for /api/images/ routes
        std::string ct;
        std::string_view target = req.target();
        bool isImagePath = (target.find("/api/images/") != std::string_view::npos);
        if (isJson)
            ct = "application/json";
        else if (isImagePath && path.size() >= 2) {
            // Detect image format from magic bytes
            auto b0 = static_cast<uint8_t>(path[0]);
            auto b1 = static_cast<uint8_t>(path[1]);
            if (b0 == 0xFF && b1 == 0xD8)
                ct = "image/jpeg";
            else if (path.size() >= 4 && b0 == 0x89 && b1 == 'P'
                     && path[2] == 'N' && path[3] == 'G')
                ct = "image/png";
            else if (path.size() >= 12 && b0 == 'R' && b1 == 'I'
                     && path[2] == 'F' && path[3] == 'F'
                     && path[8] == 'W' && path[9] == 'E'
                     && path[10] == 'B' && path[11] == 'P')
                ct = "image/webp";
            else
                ct = "application/octet-stream";
        } else
            ct = "text/html";

        res.set(http::field::content_type, ct);
        res.keep_alive(req.keep_alive());
        res.body() = path;
        res.prepare_payload();
        return res;
    } else if (type == 2 && binarybuf != nullptr) {
        http::response<http::buffer_body> res {http::status::ok, req.version()};
        apply_common_headers(res);
        res.set(http::field::content_type, "application/octet-stream");
        res.keep_alive(req.keep_alive());
        res.body().data = binarybuf->data();
        res.body().size = binarybuf->size();
        res.prepare_payload();
        return res;
    }

    if (path.empty()) {
        // Strip query string from target before building file path
        std::string_view target = req.target();
        auto queryPos = target.find('?');
        std::string_view cleanPath = (queryPos != std::string_view::npos) ? target.substr(0, queryPos) : target;
        path = path_cat(doc_root, cleanPath);
        if (cleanPath.back() == '/')
            path.append("index.html");
    }

    LOG_INFO("webserver: opening {}", path);
    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if (ec == beast::errc::no_such_file_or_directory) {
        // SPA fallback: serve index.html for non-file routes (no extension)
        // This allows the PWA client-side router to handle routes like /stock, /sales, etc.
        std::string_view target = req.target();
        // Strip query string for SPA route check
        auto queryPos = target.find('?');
        std::string_view routePath = (queryPos != std::string_view::npos) ? target.substr(0, queryPos) : target;
        auto dotPos = routePath.rfind('.');
        auto slashPos = routePath.rfind('/');
        // No file extension found (or dot is before last slash) → SPA fallback
        if (dotPos == std::string_view::npos || (slashPos != std::string_view::npos && dotPos < slashPos)) {
            // Don't fallback for API calls
            if (routePath.find("/api/") == std::string_view::npos) {
                auto indexPath = path_cat(doc_root, "index.html");
                beast::error_code ec2;
                body.open(indexPath.c_str(), beast::file_mode::scan, ec2);
                if (!ec2) {
                    path = indexPath;
                    size = body.size();
                    goto serve_file;
                }
            }
        }
        return not_found(req.target());
    }

    // Handle an unknown error
    if (ec)
        return server_error(ec.message());

    // Cache the size since we need it after the move
serve_file:
    size = body.size();
    // Respond to HEAD request
    if (req.method() == http::verb::head) {
        http::response<http::empty_body> res {http::status::ok, req.version()};
        apply_common_headers(res);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return res;
    }

    // Respond to GET request
    http::response<http::file_body> res {
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version())};
    apply_common_headers(res);
    res.set(http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return res;
}

//------------------------------------------------------------------------------

// Echoes back all received WebSocket messages.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template<class Derived>
class websocket_session {
    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    Derived& derived() {
        return static_cast<Derived&>(*this);
    }

    beast::flat_buffer buffer_;
    std::shared_ptr<std::vector<char>> res;

    // Start the asynchronous operation
    template<class Body, class Allocator>
    void do_accept(http::request<Body, http::basic_fields<Allocator>> req) {
        // Set suggested timeout settings for the websocket
        derived().ws().set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        derived().ws().set_option(
            websocket::stream_base::decorator(
                [](websocket::response_type& res) {
                    res.set(http::field::server,
                        std::string(BOOST_BEAST_VERSION_STRING) + " advanced-server-flex");
                }));

        // Accept the websocket handshake
        derived().ws().async_accept(
            req,
            beast::bind_front_handler(
                &websocket_session::on_accept,
                derived().shared_from_this()));
    }

private:
    void on_accept(beast::error_code ec) {
        if (ec)
            return fail(ec, "accept ---");

        // Read a message
        do_read();
    }

    void do_read() {
        // Read a message into our buffer
        derived().ws().async_read(
            buffer_,
            beast::bind_front_handler(
                &websocket_session::on_read,
                derived().shared_from_this()));
    }

    void dump(std::shared_ptr<std::vector<char>> res) {
#ifdef _DEBUG
        LOG_INFO("WebSocket sending: {} bytes", res->size());
#endif
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        // This indicates that the websocket_session was closed
        if (ec == websocket::error::closed)
            return;

        if (ec == beast::error::timeout) {
            do_read();  // keep reading after timeout
            return;
        }

        if (ec) return fail(ec, "read");

            // Process the message
#ifdef _DEBUG
        LOG_INFO("Received data: {}", beast::buffers_to_string(buffer_.data()));
#endif
        res = process_websocket_command(buffer_.data());
        if (res && !res->empty()) {
            auto buf = boost::asio::buffer(res->data(), res->size());
            dump(res);
            derived().ws().text(derived().ws().got_text());
            derived().ws().async_write(buf, beast::bind_front_handler(&websocket_session::on_write, derived().shared_from_this()));
        } else {
            // No response to send — clear buffer and continue reading
            buffer_.consume(buffer_.size());
            do_read();
        }
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec == beast::error::timeout) {
            return;
        }
        if (ec) return fail(ec, "write");

        // Clear the buffer
        buffer_.consume(buffer_.size());

        // Do another read
        do_read();
    }

public:
    // Start the asynchronous operation
    template<class Body, class Allocator>
    void run(http::request<Body, http::basic_fields<Allocator>> req) {
        // Accept the WebSocket upgrade request
        do_accept(std::move(req));
    }
};

//------------------------------------------------------------------------------

// Handles a plain WebSocket connection
class plain_websocket_session
  : public websocket_session<plain_websocket_session>,
    public std::enable_shared_from_this<plain_websocket_session> {
    websocket::stream<beast::tcp_stream> ws_;

public:
    // Create the session
    explicit plain_websocket_session(beast::tcp_stream&& stream)
      : ws_(std::move(stream)) {
    }

    // Called by the base class
    websocket::stream<beast::tcp_stream>& ws() { return ws_; }
};

//------------------------------------------------------------------------------

// Handles an SSL WebSocket connection
class ssl_websocket_session
  : public websocket_session<ssl_websocket_session>,
    public std::enable_shared_from_this<ssl_websocket_session> {
    websocket::stream<ssl::stream<beast::tcp_stream>> ws_;

public:
    // Create the ssl_websocket_session
    explicit ssl_websocket_session(ssl::stream<beast::tcp_stream>&& stream)
      : ws_(std::move(stream)) {
    }

    // Called by the base class
    websocket::stream<ssl::stream<beast::tcp_stream>>& ws() { return ws_; }
};

//------------------------------------------------------------------------------

template<class Body, class Allocator>
void make_websocket_session(beast::tcp_stream stream, http::request<Body, http::basic_fields<Allocator>> req) {
    std::make_shared<plain_websocket_session>(std::move(stream))->run(std::move(req));
}

template<class Body, class Allocator>
void make_websocket_session(ssl::stream<beast::tcp_stream> stream, http::request<Body, http::basic_fields<Allocator>> req) {
    std::make_shared<ssl_websocket_session>(std::move(stream))->run(std::move(req));
}

//------------------------------------------------------------------------------

// Handles an HTTP server connection.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template<class Derived>
class http_session {
    std::shared_ptr<std::string const> doc_root_;

    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    Derived& derived() { return static_cast<Derived&>(*this); }

    static constexpr std::size_t queue_limit = 8;  // max responses
    std::queue<http::message_generator> response_queue_;

    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<http::request_parser<http::string_body>> parser_;

protected:
    beast::flat_buffer buffer_;

public:
    // Construct the session
    http_session(beast::flat_buffer buffer, std::shared_ptr<std::string const> const& doc_root)
      : doc_root_(doc_root), buffer_(std::move(buffer)) {
    }

    void do_read() {
        // Construct a new parser for each message
        parser_.emplace();

        // Apply a reasonable limit to the allowed size
        // of the body in bytes to prevent abuse.
        // 1MB — sufficient for API calls with line items, stock imports, etc.
        parser_->body_limit(1000000);

        // Set the timeout.
        beast::get_lowest_layer(
            derived().stream())
            .expires_after(std::chrono::seconds(30));

        // Read a request using the parser-oriented interface
        http::async_read(
            derived().stream(),
            buffer_,
            *parser_,
            beast::bind_front_handler(
                &http_session::on_read,
                derived().shared_from_this()));
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        // This means they closed the connection
        if (ec == http::error::end_of_stream)
            return derived().do_eof();

        if (ec)
            return fail(ec, "read");

        // See if it is a WebSocket Upgrade
        if (websocket::is_upgrade(parser_->get())) {
            // Disable the timeout.
            // The websocket::stream uses its own timeout settings.
            beast::get_lowest_layer(derived().stream()).expires_never();

            // Create a websocket session, transferring ownership
            // of both the socket and the HTTP request.
            return make_websocket_session(
                derived().release_stream(),
                parser_->release());
        }

        // Send the response
        queue_write(handle_request(*doc_root_, parser_->release()));

        // If we aren't at the queue limit, try to pipeline another request
        if (response_queue_.size() < queue_limit)
            do_read();
    }

    void queue_write(http::message_generator response) {
        // Allocate and store the work
        response_queue_.push(std::move(response));

        // If there was no previous work, start the write loop
        if (response_queue_.size() == 1)
            do_write();
    }

    // Called to start/continue the write-loop. Should not be called when
    // write_loop is already active.
    void do_write() {
        if (!response_queue_.empty()) {
            bool keep_alive = response_queue_.front().keep_alive();

            beast::async_write(
                derived().stream(),
                std::move(response_queue_.front()),
                beast::bind_front_handler(
                    &http_session::on_write,
                    derived().shared_from_this(),
                    keep_alive));
        }
    }

    void on_write(bool keep_alive, beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        if (!keep_alive) {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return derived().do_eof();
        }

        // Resume the read if it has been paused
        if (response_queue_.size() == queue_limit)
            do_read();

        response_queue_.pop();

        do_write();
    }
};

//------------------------------------------------------------------------------

// Handles a plain HTTP connection
class plain_http_session
  : public http_session<plain_http_session>,
    public std::enable_shared_from_this<plain_http_session> {
    beast::tcp_stream stream_;

public:
    // Create the session
    plain_http_session(
        beast::tcp_stream&& stream,
        beast::flat_buffer&& buffer,
        std::shared_ptr<std::string const> const& doc_root)
      : http_session<plain_http_session>(
            std::move(buffer),
            doc_root),
        stream_(std::move(stream)) {
    }

    // Start the session
    void run() { this->do_read(); }

    // Called by the base class
    beast::tcp_stream& stream() { return stream_; }

    // Called by the base class
    beast::tcp_stream release_stream() { return std::move(stream_); }

    // Called by the base class
    void do_eof() {
        // Send a TCP shutdown
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    }
};

//------------------------------------------------------------------------------

// Handles an SSL HTTP connection
class ssl_http_session
  : public http_session<ssl_http_session>,
    public std::enable_shared_from_this<ssl_http_session> {
    ssl::stream<beast::tcp_stream> stream_;

public:
    // Create the http_session
    ssl_http_session(
        beast::tcp_stream&& stream,
        ssl::context& ctx,
        beast::flat_buffer&& buffer,
        std::shared_ptr<std::string const> const& doc_root)
      : http_session<ssl_http_session>(
            std::move(buffer),
            doc_root),
        stream_(std::move(stream), ctx) {
    }

    // Start the session
    void run() {
        // Set the timeout.
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

        // Perform the SSL handshake
        // Note, this is the buffered version of the handshake.
        stream_.async_handshake(
            ssl::stream_base::server,
            buffer_.data(),
            beast::bind_front_handler(
                &ssl_http_session::on_handshake,
                shared_from_this()));
    }

    // Called by the base class
    ssl::stream<beast::tcp_stream>& stream() { return stream_; }

    // Called by the base class
    ssl::stream<beast::tcp_stream> release_stream() { return std::move(stream_); }

    // Called by the base class
    void do_eof() {
        // Set the timeout.
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

        // Perform the SSL shutdown
        stream_.async_shutdown(
            beast::bind_front_handler(
                &ssl_http_session::on_shutdown,
                shared_from_this()));
    }

private:
    void on_handshake(beast::error_code ec, std::size_t bytes_used) {
        if (ec)
            return fail(ec, "handshake");

        // Consume the portion of the buffer used by the handshake
        buffer_.consume(bytes_used);

        do_read();
    }

    void on_shutdown(beast::error_code ec) {
        if (ec)
            return fail(ec, "shutdown");

        // At this point the connection is closed gracefully
    }
};

//------------------------------------------------------------------------------

// Detects SSL handshakes
class detect_session : public std::enable_shared_from_this<detect_session> {
    beast::tcp_stream stream_;
    ssl::context& ctx_;
    std::shared_ptr<std::string const> doc_root_;
    beast::flat_buffer buffer_;

public:
    explicit detect_session(
        tcp::socket&& socket,
        ssl::context& ctx,
        std::shared_ptr<std::string const> const& doc_root)
      : stream_(std::move(socket)), ctx_(ctx), doc_root_(doc_root) {
    }

    // Launch the detector
    void run() {
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session. Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        net::dispatch(
            stream_.get_executor(),
            beast::bind_front_handler(
                &detect_session::on_run,
                this->shared_from_this()));
    }

    void on_run() {
        // Set the timeout.
        stream_.expires_after(std::chrono::seconds(30));

        beast::async_detect_ssl(
            stream_,
            buffer_,
            beast::bind_front_handler(
                &detect_session::on_detect,
                this->shared_from_this()));
    }

    void on_detect(beast::error_code ec, bool result) {
        if (ec)
            return fail(ec, "detect");

        if (result) {
            // Launch SSL session
            std::make_shared<ssl_http_session>(
                std::move(stream_),
                ctx_,
                std::move(buffer_),
                doc_root_)
                ->run();
            return;
        }

        // Launch plain session
        std::make_shared<plain_http_session>(
            std::move(stream_),
            std::move(buffer_),
            doc_root_)
            ->run();
    }
};

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener> {
    net::io_context& ioc_;
    ssl::context& ctx_;
    tcp::acceptor acceptor_;
    std::shared_ptr<std::string const> doc_root_;
public:
    bool isValid {false};
    listener(
        net::io_context& ioc,
        ssl::context& ctx,
        tcp::endpoint endpoint,
        std::shared_ptr<std::string const> const& doc_root)
      : ioc_(ioc), ctx_(ctx), acceptor_(net::make_strand(ioc)), doc_root_(doc_root) {
        beast::error_code ec;

        // Open the acceptor
        acceptor_.open(endpoint.protocol(), ec);
        if (ec) {
            fail(ec, "open");
            return;
        }

        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec) {
            fail(ec, "set_option");
            return;
        }

        // Bind to the server address
        acceptor_.bind(endpoint, ec);
        if (ec) {
            fail(ec, "bind");
            return;
        }

        // Start listening for connections
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) {
            fail(ec, "listen");
            return;
        }
        isValid = true;
    }

    // Start accepting incoming connections
    void run() {
        do_accept();
    }

private:
    void do_accept() {
        // The new connection gets its own strand
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(
                &listener::on_accept,
                shared_from_this()));
    }

    void on_accept(beast::error_code ec, tcp::socket socket) {
        if (!ec) {
            try {
                std::make_shared<detect_session>(
                    std::move(socket),
                    ctx_,
                    doc_root_)
                    ->run();
            } catch (const std::exception& e) {
                LOG_ERROR("listener::on_accept — failed to create session: {}", e.what());
            } catch (...) {
                LOG_ERROR("listener::on_accept — unknown exception creating session");
            }
        }

        // Accept another connection
        do_accept();
    }
};

//------------------------------------------------------------------------------
// Server lifecycle management — thread-safe start/stop
static std::shared_ptr<net::io_context> g_ioc {nullptr};
static std::mutex g_ioc_mutex;
static std::atomic<bool> g_server_running {false};

void StartFlexWebServer(const std::string ip, unsigned short port, std::string_view wwwroot, int threads, const std::string& certChainFile, const std::string& privateKeyFile, const std::string& verifyFile) {
    // Capture wwwroot by value since it's a string_view
    std::thread([=, wwwroot_str = std::string(wwwroot)] {
        try {
            auto const address = net::ip::make_address(ip);
            auto const doc_root = std::make_shared<std::string>(wwwroot_str);

            LOG_INFO("Starting http/ws server at {} on port {}", ip, port);
            LOG_INFO("http root folder: {}", wwwroot_str);
            std::filesystem::create_directories(wwwroot_str);

            // The io_context is required for all I/O
            auto local_ioc = std::make_shared<net::io_context>(threads);
            {
                std::lock_guard<std::mutex> lock(g_ioc_mutex);
                g_ioc = local_ioc;
            }

            // The SSL context is required, and holds certificates
            ssl::context ctx {ssl::context::tlsv12};

            if (!certChainFile.empty() && !privateKeyFile.empty()) {
                try {
                    ctx.use_certificate_chain_file(certChainFile);
                    ctx.use_private_key_file(privateKeyFile, boost::asio::ssl::context::pem);
                    if (!verifyFile.empty()) ctx.load_verify_file(verifyFile);
                    LOG_INFO("webserver: loaded cert(ctx) from {}, {}", certChainFile, privateKeyFile);
                } catch (boost::system::system_error &) {
                    load_server_certificate(ctx);
                    LOG_WARN("webserver: loaded cert(ctx) from self-signed ssl keys");
                }
            } else
                load_server_certificate(ctx);

            // Create and launch a listening port
            auto watcher = std::make_shared<listener>(*local_ioc, ctx, tcp::endpoint {address, port}, doc_root);

            if (watcher->isValid) {
                g_server_running.store(true, std::memory_order_release);
                watcher->run();

                // Run the I/O service on the requested number of threads
                std::vector<std::thread> v;
                v.reserve(threads - 1);
                for (auto i = threads - 1; i > 0; --i)
                    v.emplace_back([&local_ioc] { local_ioc->run(); });
                local_ioc->run();

                // Block until all the threads exit
                for (auto& t : v)
                    t.join();
            } else {
                LOG_ERROR("Listener failed to listen on {}: {}", ip, port);
            }
            g_server_running.store(false, std::memory_order_release);
        } catch (const std::exception& e) {
            LOG_ERROR("StartFlexWebServer exception: {}", e.what());
            g_server_running.store(false, std::memory_order_release);
        } catch (...) {
            LOG_ERROR("StartFlexWebServer: Unknown exception");
            g_server_running.store(false, std::memory_order_release);
        }
    }).detach();
}

void StopFlexWebServer() {
    LOG_INFO("FlexWebServer: Stopping Web Server...");
    std::shared_ptr<net::io_context> local_ioc;
    {
        std::lock_guard<std::mutex> lock(g_ioc_mutex);
        local_ioc = g_ioc;
        g_ioc.reset();
    }
    if (local_ioc) {
        LOG_INFO("FlexWebServer: stopping io_context...");
        local_ioc->stop();
    }
    // Wait briefly for server to actually stop
    int wait_count = 0;
    while (g_server_running.load(std::memory_order_acquire) && wait_count < 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        wait_count++;
    }
    LOG_INFO("FlexWebServer: stopped!");
}
