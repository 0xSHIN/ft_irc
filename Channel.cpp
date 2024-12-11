// Channel.cpp
#include "Channel.hpp"
#include "Commands.hpp"
#include <iostream>
#include <algorithm> // For std::remove
#include <sstream>
#include <sys/socket.h>

std::map<int, User> users;
std::map<std::string, struct Channel> channels;
std::set<int> operators; 

void sendMessage(int clientSockfd, const std::string& message) {
    send(clientSockfd, message.c_str(), message.length(), 0);
}

std::string intToString(int value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

// Handle the KICK command
void handleKick(int clientSockfd, const std::string& targetNick, const std::string& channelName) {
    if (!isChannelOperator(clientSockfd, channelName)) {
        sendMessage(clientSockfd, "You are not an operator in this channel.\n");
        return;
    }

    int targetClientSockfd = findClientByNick(targetNick);
    if (targetClientSockfd == -1 || !isClientInChannel(targetClientSockfd, channelName)) {
        sendMessage(clientSockfd, "Client is not in the channel.\n");
        return;
    }

    Channel& channel = channels[channelName];
    
    // Remove the client from the channel
    channel.clients.erase(std::remove(channel.clients.begin(), channel.clients.end(), targetClientSockfd), channel.clients.end());
    
    // Remove the channel from the user's list
    users[targetClientSockfd].channels.erase(std::remove(users[targetClientSockfd].channels.begin(), users[targetClientSockfd].channels.end(), channelName), users[targetClientSockfd].channels.end());

    // Notify all users in the channel about the kick
    std::string kickMessage = targetNick + " has been kicked from " + channelName + ".\n";
    for (std::vector<int>::iterator it = channel.clients.begin(); it != channel.clients.end(); ++it) {
        sendMessage(*it, kickMessage);
    }
}

void handleInvite(int clientSockfd, const std::string& targetNick, const std::string& channelName) {
    if (!isChannelOperator(clientSockfd, channelName)) {
        sendMessage(clientSockfd, "You are not an operator in this channel.\n");
        return;
    }

    Channel& channel = channels[channelName];
    if (channel.inviteOnly) {
        int targetClientSockfd = findClientByNick(targetNick);
        if (targetClientSockfd == -1) {
            sendMessage(clientSockfd, "Client not found.\n");
            return;
        }

        // Add client to the invited list
        channel.invitedUsers.push_back(targetClientSockfd);
        
        // Notify the invited user
        sendMessage(targetClientSockfd, "You have been invited to join " + channelName + ".\n");
        sendMessage(clientSockfd, "Client " + targetNick + " has been invited to " + channelName + ".\n");
    } else {
        sendMessage(clientSockfd, "Channel is not invite-only.\n");
    }
}

// Handle the TOPIC command
void handleTopic(int clientSockfd, const std::string& channelName, const std::string& newTopic) {
    std::map<std::string, Channel>::iterator it = channels.find(channelName);
    
    if (it == channels.end()) {
        send(clientSockfd, "Error: No such channel.\n", 24, 0);
        return;
    }

    Channel& channel = it->second;

    if (!newTopic.empty()) {
        // Check if user is a channel operator before allowing them to set the topic
        if (!isChannelOperator(clientSockfd, channelName)) {
            send(clientSockfd, "Error: You do not have permission to change the topic.\n", 55, 0);
            return;
        }

        // Set the new topic
        channel.topic = newTopic;
        
        // Notify all clients in the channel about the new topic
        std::ostringstream oss;
        oss << ":" << users[clientSockfd].nickname << " TOPIC " << channelName << " :" << newTopic << "\n";
        
        for (std::vector<int>::iterator clientIt = channel.clients.begin(); clientIt != channel.clients.end(); ++clientIt) {
            send(*clientIt, oss.str().c_str(), oss.str().length(), 0);
        }
        
    } else {
        // If no new topic is provided, just show the current topic
        if (channel.topic.empty()) {
            send(clientSockfd, "No topic is set for this channel.\n", 35, 0);
        } else {
            std::ostringstream oss;
            oss << "Current topic for " << channelName << ": " << channel.topic << "\n";
            send(clientSockfd, oss.str().c_str(), oss.str().length(), 0);
        }
    }
}



// void handleTopic(int clientSockfd, const std::string& channelName, const std::string& newTopic) {
//     if (newTopic.empty()) {
//         std::cout << "Current topic: " << channels[channelName].topic << std::endl;
//     } else {
//         if (!isChannelOperator(clientSockfd, channelName)) {
//             std::cout << "You are not an operator and can't change the topic." << std::endl;
//             return;
//         }
//         channels[channelName].topic = newTopic;
//         std::cout << "Topic for " << channelName << " has been changed to: " << newTopic << std::endl;
//     }
// }

void handleMode(int clientSockfd, const std::string& channelName, const std::string& mode, const std::string& param) {
    if (!isChannelOperator(clientSockfd, channelName)) {
        sendMessage(clientSockfd, "You are not an operator in this channel.\n");
        return;
    }

    Channel& channel = channels[channelName];

    if (mode == "-i") {
        // Toggle Invite-only mode
        channel.inviteOnly = !channel.inviteOnly;
        std::string status = (channel.inviteOnly ? "on" : "off");
        
        for (std::vector<int>::iterator it = channel.clients.begin(); it != channel.clients.end(); ++it) {
            sendMessage(*it, "Invite-only mode set to " + status + " for " + channelName + ".\n");
        }
        
    } else if (mode == "-t") {
        // Toggle restriction of topic command to operators only
        channel.topicRestricted = !channel.topicRestricted;
        std::string status = (channel.topicRestricted ? "operators only" : "anyone");

        for (std::vector<int>::iterator it = channel.clients.begin(); it != channel.clients.end(); ++it) {
            sendMessage(*it, "Topic restriction set to " + status + " for " + channelName + ".\n");
        }
        
    } else if (mode == "-k") {
        // Set or remove the channel key (password)
        if (param.empty()) {
            sendMessage(clientSockfd, "Channel key removed.\n");
            channel.key = "";
            for (std::vector<int>::iterator it = channel.clients.begin(); it != channel.clients.end(); ++it) {
                sendMessage(*it, "Channel key has been removed for " + channelName + ".\n");
            }
        } else {
            channel.key = param;
            for (std::vector<int>::iterator it = channel.clients.begin(); it != channel.clients.end(); ++it) {
                sendMessage(*it, "Channel key set to: " + param + "\n");
            }
        }

    } else if (mode == "-l") {
        // Set the user limit for the channel
        int userLimit;
        
        std::istringstream iss(param);
        if (!(iss >> userLimit)) {
            sendMessage(clientSockfd, "Invalid user limit.\n");
            return;
        }
        
        channel.userLimit = userLimit;

        for (std::vector<int>::iterator it = channels[channelName].clients.begin(); it != channels[channelName].clients.end(); ++it) {
            sendMessage(*it, "User limit set to " + intToString(userLimit) + " for " + channelName + ".\n");
        }

    } else if (mode == "-o") {
        // Grant or take operator privilege from a user
        int targetClientSocket = findClientByNick(param); // Assuming param is a nickname
        
        if (targetClientSocket == -1) {
            sendMessage(clientSockfd, "Invalid nickname.\n");
            return;
        }

        if (isChannelOperator(targetClientSocket, channelName)) {
            // Remove operator privilege
            channels[channelName].operators.erase(
                std::remove(channels[channelName].operators.begin(), channels[channelName].operators.end(), targetClientSocket),
                channels[channelName].operators.end());
                
            sendMessage(clientSockfd, param + "'s operator privilege has been removed.\n");

            for (std::vector<int>::iterator it = channels[channelName].clients.begin(); it != channels[channelName].clients.end(); ++it) {
                sendMessage(*it, param + "'s operator privilege has been removed in "+channelName+".\n");
            }
            
        } else {
            // Grant operator privilege
            channels[channelName].operators.push_back(targetClientSocket);
            sendMessage(clientSockfd, param + "'s operator privilege has been granted.\n");

            for (std::vector<int>::iterator it = channels[channelName].clients.begin(); it != channels[channelName].clients.end(); ++it) {
                sendMessage(*it, param + "'s operator privilege has been granted in "+channelName+".\n");
            }
            
        }
    } else {
         sendMessage(clientSockfd,"Unknown mode: "+mode+"\n");
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


// void createChannel(const std::string& channelName) {
//     // Check if the channel already exists
//     if (channels.find(channelName) != channels.end()) {
//         std::cout << "Channel " << channelName << " already exists." << std::endl;
//         return;
//     }

//     // Create a new Channel
//     Channel newChannel;
//     newChannel.name = channelName;

//     // Add the new channel to the channels map
//     channels[channelName] = newChannel;

//     std::cout << "Channel " << channelName << " has been created successfully." << std::endl;
// }

// void joinChannel(int clientSockfd, const std::string& channelName) {
//     // Ensure the channel exists
//     if (channels.find(channelName) == channels.end()) {
//         std::cout << "Channel " << channelName << " does not exist. Creating it now." << std::endl;
//         createChannel(channelName);
//     }

//     // Add the client to the channel's client list
//     channels[channelName].clients.push_back(clientSockfd);
//     std::cout << "Client " << clientSockfd << " has joined channel " << channelName << "." << std::endl;
// }
