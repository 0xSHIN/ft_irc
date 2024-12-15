#include "Channel.hpp"
#include <cstring>
#include <sstream>
#include <algorithm>
#include <sys/socket.h>

std::map<int, Client> clients;
std::map<std::string, Channel> channels;
std::set<int> operators;

void sendMessage(int clientSockfd, const std::string& message) {
    send(clientSockfd, message.c_str(), message.length(), 0);
}

std::string intToString(int value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

void broadcastToChannel(Channel& channel, const std::string& message, int excludeSockfd) {
    for (std::vector<int>::iterator it = channel.clients.begin(); it != channel.clients.end(); ++it) {
        if (*it != excludeSockfd) {
            sendMessage(*it, message);
        }
    }
}

void sendMessageRFC(int clientSockfd, const std::string &prefix, const std::string &command, const std::string &params, const std::string &trailing) {
    std::ostringstream oss;
    if (!prefix.empty()) oss << ":" << prefix << " ";
    oss << command;
    if (!params.empty()) oss << " " << params;
    if (!trailing.empty()) oss << " :" << trailing;
    oss << "\r\n";
    send(clientSockfd, oss.str().c_str(), oss.str().length(), 0);
}

void handleKick(int clientSockfd, const std::string& channelName, const std::string& targetNick) {
    if (!doesChannelExist(channelName)) {
        // Send error: No such channel (Numeric 403)
        sendMessage(clientSockfd, ":localhost 403 " + clients[clientSockfd].nickname + " " + channelName + " :No such channel\r\n");
        return;
    }

    if (!isChannelOperator(clientSockfd, channelName)) {
        // Send error: You're not channel operator (Numeric 482)
        sendMessage(clientSockfd, ":localhost 482 " + clients[clientSockfd].nickname + " " + channelName + " :You're not a channel operator\r\n");
        return;
    }

    int targetClientSockfd = findClientByNick(targetNick);
    if (targetClientSockfd == -1 || !isClientInChannel(targetClientSockfd, channelName)) {
        // Send error: User not in channel (Numeric 441)
        sendMessage(clientSockfd, ":localhost 441 " + clients[clientSockfd].nickname + " " + targetNick + " " + channelName + " :They are not on that channel\r\n");
        return;
    }

    Channel& channel = channels[channelName];

    // Remove the client from the channel
    channel.clients.erase(std::remove(channel.clients.begin(), channel.clients.end(), targetClientSockfd), channel.clients.end());

    // Remove the channel from the user's list
    clients[targetClientSockfd].channels.erase(std::remove(clients[targetClientSockfd].channels.begin(), clients[targetClientSockfd].channels.end(), channelName), clients[targetClientSockfd].channels.end());

    // Notify all users in the channel about the kick
    std::ostringstream kickMessage;
    kickMessage << ":" << clients[clientSockfd].nickname << "!~" << clients[clientSockfd].username 
                << "@" << clients[clientSockfd].hostname 
                << " KICK " << channelName << " " << targetNick << " :Kicked by operator\r\n";

    broadcastToChannel(channel, kickMessage.str(), -1);

    // Notify the kicked user
    sendMessage(targetClientSockfd, kickMessage.str());
}


void handleTopic(int clientSockfd, const std::string& channelName, const std::string& newTopic) {
    std::map<std::string, Channel>::iterator it = channels.find(channelName);

    if (it == channels.end()) {
        // Send error: No such channel (Numeric 403)
        sendMessage(clientSockfd, ":localhost 403 " + clients[clientSockfd].nickname + " " + channelName + " :No such channel\r\n");
        return;
    }

    Channel& channel = it->second;

    if (!newTopic.empty()) {
        // Check if the user is allowed to set the topic
        if (channel.topicRestricted && !isChannelOperator(clientSockfd, channelName)) {
            // Send error: No permission to change the topic (Numeric 482)
            sendMessage(clientSockfd, ":localhost 482 " + clients[clientSockfd].nickname + " " + channelName + " :You do not have permission to change the topic\r\n");
            return;
        }

        // Set the new topic
        channel.topic = newTopic;

        // Notify all clients in the channel about the new topic
        std::ostringstream topicMessage;
        topicMessage << ":" << clients[clientSockfd].nickname << "!~" << clients[clientSockfd].username 
                     << "@" << clients[clientSockfd].hostname 
                     << " TOPIC " << channelName << " :" << newTopic << "\r\n";

        broadcastToChannel(channel, topicMessage.str(), -1);
    } else {
        // No new topic provided: respond with the current topic or no topic
        if (channel.topic.empty()) {
            // Send response: No topic set (Numeric 331)
            sendMessage(clientSockfd, ":localhost 331 " + clients[clientSockfd].nickname + " " + channelName + " :No topic is set\r\n");
        } else {
            // Send response: Current topic (Numeric 332)
            sendMessage(clientSockfd, ":localhost 332 " + clients[clientSockfd].nickname + " " + channelName + " :" + channel.topic + "\r\n");
        }
    }
}

void handleMode(int clientSockfd, const std::string& channelName, const std::string& mode, const std::string& param) {
    if (!isChannelOperator(clientSockfd, channelName)) {
        sendMessage(clientSockfd, ":localhost 482 " + clients[clientSockfd].nickname + " " + channelName + " :You are not a channel operator.\r\n");
        return;
    }

    std::map<std::string, Channel>::iterator channelIt = channels.find(channelName);
    if (channelIt == channels.end()) {
        sendMessage(clientSockfd, ":localhost 403 " + clients[clientSockfd].nickname + " " + channelName + " :No such channel\r\n");
        return;
    }

    Channel& channel = channelIt->second;

    if (mode == "-i") {
        // Toggle invite-only mode
        channel.inviteOnly = !channel.inviteOnly;
        std::string status = channel.inviteOnly ? "+i" : "-i";

        std::string modeMessage = ":localhost MODE " + channelName + " " + status + "\r\n";
        broadcastToChannel(channel, modeMessage, -1); // Broadcast to all channel members
    } else if (mode == "-t") {
        // Toggle topic restriction
        channel.topicRestricted = !channel.topicRestricted;
        std::string status = channel.topicRestricted ? "+t" : "-t";

        std::string modeMessage = ":localhost MODE " + channelName + " " + status + "\r\n";
        broadcastToChannel(channel, modeMessage, -1); // Broadcast to all channel members
    } else if (mode == "-k") {
        // Set or remove the channel key (password)
        if (param.empty()) {
            channel.key.clear();
            std::string modeMessage = ":localhost MODE " + channelName + " -k\r\n";
            broadcastToChannel(channel, modeMessage, -1);
        } else {
            channel.key = param;
            std::string modeMessage = ":localhost MODE " + channelName + " +k " + param + "\r\n";
            broadcastToChannel(channel, modeMessage, -1);
        }
    } else if (mode == "-l") {
        // Set user limit for the channel
        int userLimit = 0;
        std::istringstream iss(param);
        if (!(iss >> userLimit)) {
            sendMessage(clientSockfd, ":localhost 461 " + clients[clientSockfd].nickname + " MODE :Invalid user limit parameter\r\n");
            return;
        }
        channel.userLimit = userLimit;

        std::string modeMessage = ":localhost MODE " + channelName + " +l " + param + "\r\n";
        broadcastToChannel(channel, modeMessage, -1);
    } else if (mode == "-o") {
        // Grant or revoke operator status
        int targetClientSockfd = findClientByNick(param);
        if (targetClientSockfd == -1) {
            sendMessage(clientSockfd, ":localhost 401 " + clients[clientSockfd].nickname + " " + param + " :No such nick\r\n");
            return;
        }

        if (isChannelOperator(targetClientSockfd, channelName)) {
            // Remove operator
            channel.operators.erase(std::remove(channel.operators.begin(), channel.operators.end(), targetClientSockfd), channel.operators.end());
            std::string modeMessage = ":localhost MODE " + channelName + " -o " + clients[targetClientSockfd].nickname + "\r\n";
            broadcastToChannel(channel, modeMessage, -1);
        } else {
            // Grant operator
            channel.operators.push_back(targetClientSockfd);
            std::string modeMessage = ":localhost MODE " + channelName + " +o " + clients[targetClientSockfd].nickname + "\r\n";
            broadcastToChannel(channel, modeMessage, -1);
        }
    } else {
        sendMessage(clientSockfd, ":localhost 472 " + mode + " :is unknown mode char to me\r\n");
    }
}



bool isChannelOperator(int clientSockfd, const std::string& channelName) {
    std::map<std::string, Channel>::iterator it = channels.find(channelName);
    if (it != channels.end()) {
        return std::find(it->second.operators.begin(), it->second.operators.end(), clientSockfd) != it->second.operators.end();
    }
    return false;
}

bool isClientInChannel(int clientSockfd, const std::string& channelName) {
    std::map<std::string, Channel>::iterator it = channels.find(channelName);
    if (it != channels.end()) {
        return std::find(it->second.clients.begin(), it->second.clients.end(), clientSockfd) != it->second.clients.end();
    }
    return false;
}

bool doesChannelExist(const std::string& channelName) {
    return channels.find(channelName) != channels.end();
}
