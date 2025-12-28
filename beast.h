#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <string>
#include <vector>

extern void StartWebServer(const std::string& ipAddress, unsigned short port, const std::string& homeDir, int noOfThread, const std::string& certChainFile, const std::string& privateKeyFile, const std::string& verifyFile);
extern void StopWebServer();
extern void StartWebSocketServer(const std::string& ipAddress, unsigned short port, int noOfThread);
extern void StopWebSocketServer();
extern void StartFlexWebServer(const std::string ip, unsigned short port, std::string_view wwwroot, int threads, const std::string& certChainFile, const std::string& privateKeyFile, const std::string& verifyFile = "");
extern void StopFlexWebServer();

extern std::tuple<int, std::string, std::shared_ptr<std::vector<char>>> process_web_command(boost::beast::http::verb method, boost::beast::string_view command, boost::beast::string_view body, std::function<boost::beast::string_view(boost::beast::string_view key)> fnGetHeader);
extern std::shared_ptr<std::vector<char>> process_websocket_command(boost::asio::const_buffer data);
//extern void CreateNonExistingFolders(const std::string& homeDir);
