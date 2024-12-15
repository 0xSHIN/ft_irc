#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include "Kek.hpp"
#include <map>
#include <string>
#include <vector>
#include <set>

class Channel {
public:
    std::string name;
    std::string topic;
    std::vector<int> invitedUsers;
    std::vector<char> _mode;
    int userLimit;
    std::string key;
    std::vector<int> clients;
    std::vector<int> operators;
    bool inviteOnly;
    bool topicRestricted;

    Channel() : userLimit(0), inviteOnly(false), topicRestricted(false) {}
    Channel(const std::string& channelName)
        : name(channelName), topic(""), userLimit(10), key(""), inviteOnly(false), topicRestricted(false) {}
};

extern std::map<int, Client> clients;
extern std::map<std::string, Channel> channels;
extern int connectionCount;
extern std::set<int> operators;

void handleKick(int clientSockfd, const std::string& targetNick, const std::string& channelName);
void handleInvite(int clientSockfd, const std::string& targetNick, const std::string& channelName);
void handleTopic(int clientSockfd, const std::string& channelName, const std::string& newTopic);
void handleMode(int clientSockfd, const std::string& channelName, const std::string& mode, const std::string& param);
int findClientByNick(const std::string& nick);
bool isChannelOperator(int clientSockfd, const std::string& channelName);
bool isClientInChannel(int clientSockfd, const std::string& channelName);
bool doesChannelExist(const std::string& channelName);
void sendMessage(int clientSockfd, const std::string& message);

#endif // CHANNEL_HPP
