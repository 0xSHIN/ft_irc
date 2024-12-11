// Commands.hpp
#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <string>
#include <map>
#include <iostream>
#include <algorithm>
#include <sstream> // For stringstream
#include <set>
#include "Channel.hpp"

bool isOperator(int clientSockfd);
void processMessage(const std::string& message, int clientSockfd);
int findClientByNick(const std::string& nick);
void createChannel(int clientSockfd, const std::string& channelName);
void handlePrivMsg(int clientSockfd, const std::string& targetNick, const std::string& message);

#endif // COMMANDS_HPP

