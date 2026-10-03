#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <functional>
#include <string>
#include <vector>

extern void StartWebServer(const std::string& ipAddress, unsigned short port, const std::string& homeDir, int noOfThread, const std::string& certChainFile, const std::string& privateKeyFile, const std::string& verifyFile);
extern void StopWebServer();
extern void StartWebSocketServer(const std::string& ipAddress, unsigned short port, int noOfThread);
extern void StopWebSocketServer();
extern void StartFlexWebServer(const std::string ip, unsigned short port, std::string_view wwwroot, int threads, const std::string& certChainFile, const std::string& privateKeyFile, const std::string& verifyFile = "");
extern void StopFlexWebServer();

// WebSocket push channel handed to process_websocket_command by the flex
// server. `connId` uniquely identifies the connection; `push` may be called
// from ANY thread to enqueue a text frame onto the connection (serialized
// internally on the connection's strand). Used by the gateway's event fan-out.
struct WsChannel {
    std::string connId;
    std::function<void(std::string)> push;
};

extern std::tuple<int, std::string, std::shared_ptr<std::vector<char>>> process_web_command(boost::beast::http::verb method, boost::beast::string_view command, boost::beast::string_view body, std::function<boost::beast::string_view(boost::beast::string_view key)> fnGetHeader);
extern std::shared_ptr<std::vector<char>> process_websocket_command(boost::asio::const_buffer data, const WsChannel& channel);
// Called exactly once when a WebSocket connection terminates; lets handlers
// clean up per-connection state (e.g. unsubscribe event fan-out).
extern void process_websocket_closed(const std::string& connId);
//extern void CreateNonExistingFolders(const std::string& homeDir);
