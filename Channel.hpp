#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <vector>
#include <map>
#include "Commands.hpp"

// Define a struct to represent a channel
struct Channel {
    std::string name;
    std::string topic;
    std::vector<int> invitedUsers;
    bool inviteOnly;
    bool topicRestricted;
    int userLimit;
    std::string key;
    std::vector<int> clients;    // Store connected clients' socket IDs
    std::vector<int> operators;  // Store operator socket IDs
    // Default constructor
    Channel() : name(""), topic(""), inviteOnly(false), userLimit(10), key("") {}

    // Parameterized constructor
    Channel(const std::string& channelName)
        : name(channelName), topic(""), inviteOnly(false), userLimit(10), key("") {}
};

struct User {
    int sockfd;
    std::string nickname;
    std::vector<std::string> channels;
};

extern std::map<int, User> users;
extern std::map<std::string, struct Channel> channels;
extern int connectionCount;
extern std::set<int> operators; 

void handleKick(int clientSockfd, const std::string& targetNick, const std::string& channelName);
void handleInvite(int clientSockfd, const std::string& targetNick, const std::string& channelName);
void handleTopic(int clientSockfd, const std::string& channelName, const std::string& newTopic);
void handleMode(int clientSockfd, const std::string& channelName, const std::string& mode, const std::string& param);
int findClientByNick(const std::string& nick);
bool isChannelOperator(int clientSockfd, const std::string& channelName);
bool isClientInChannel(int clientSockfd, const std::string& channelName);

#endif // CHANNEL_HPP
