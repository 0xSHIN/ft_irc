#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "Channel.hpp"

bool isOperator(int clientSockfd);
void processMessage(const std::string& message, int clientSockfd);
int findClientByNick(const std::string& nick);
void createChannel(int clientSockfd, const std::string& channelName);
void handlePrivMsg(int clientSockfd, const std::string& targetNick, const std::string& message);
void handleChatMsg(int clientSockfd, const std::string& channelName, const std::string& message);
void handleNick(int clientSockfd, const std::string& newNick);
void sendMessage(int clientSockfd, const std::string& message);

#endif // COMMANDS_HPP
