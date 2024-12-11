#include "Commands.hpp"
#include "Channel.hpp"
#include <sys/socket.h>

int connectionCount = 0; 

std::map<int, std::string> clientNicks;
std::map<std::string, int> nickToFd;

int findClientByNick(const std::string& nick) {
    std::map<std::string, int>::const_iterator it;
    for (it = nickToFd.begin(); it != nickToFd.end(); ++it) {
        if (it->first == nick) {
            return it->second;  // Return the socket fd if the nickname is found
        }
    }
    return -1;  // Return -1 if the nickname is not found
}

void handleNick(int clientSockfd, const std::string& newNick) {
    // Check if the nickname is already in use
    if (nickToFd.find(newNick) != nickToFd.end()) {
        std::string response = "Nickname already in use.\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
        return;
    }

    // Remove the old nickname if it exists
    for (std::map<std::string, int>::iterator it = nickToFd.begin(); it != nickToFd.end(); ++it) {
        if (it->second == clientSockfd) {
            nickToFd.erase(it);
            break;
        }
    }

    // Set the new nickname
    nickToFd[newNick] = clientSockfd;
    clientNicks[clientSockfd] = newNick;
    std::string response = "Nickname set to " + newNick + "\n";
    send(clientSockfd, response.c_str(), response.length(), 0);
}


bool isOperator(int clientSockfd) {
    return operators.find(clientSockfd) != operators.end();
}

// Define the global channels variable
// Set of operator client socket IDs

// Function to check if a client is an operator
// bool isOperator(int clientSockfd) {
//     return reference.find(clientSockfd) != reference.end();
// }


// Function to create a new channel
void createChannel(int clientSockfd, const std::string& channelName) {
    // Check if the channel name is valid (starts with # and contains only allowed characters)
    if (channelName.empty() || channelName[0] != '#' || channelName.length() > 50) {
        std::string response = "Invalid channel name. It must start with # and be 1-50 characters long.\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
        return;
    }

    // Check if the channel already exists
    std::map<std::string, Channel>::iterator it = channels.find(channelName);
    if (it != channels.end()) {
        std::string response = "Channel " + channelName + " already exists.\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
        return;
    }

    // Create the new channel
    Channel newChannel;
    newChannel.name = channelName;
    channels[channelName] = newChannel;

    // Add the creator to the channel and make them an operator
    channels[channelName].clients.push_back(clientSockfd);
    channels[channelName].operators.push_back(clientSockfd);

    // Send confirmation to the client
    std::string response = "Channel " + channelName + " has been created successfully. You are now an operator.\n";
    send(clientSockfd, response.c_str(), response.length(), 0);

    std::cout << "Channel " << channelName << " has been created by client " << clientSockfd << "." << std::endl;
}

std::string canJoinChannel(int clientSockfd, const std::string& channelName, const std::string& password) {
    std::map<std::string, Channel>::iterator it = channels.find(channelName);
    if (it == channels.end()) {
        return ""; // Channel doesn't exist, so it can be created
    }

    Channel& channel = it->second;

    // Check invite-only mode
    if (channel.inviteOnly) {
        if (std::find(channel.invitedUsers.begin(), channel.invitedUsers.end(), clientSockfd) == channel.invitedUsers.end()) {
            return "Cannot join channel " + channelName + ": You must be invited.";
        }
    }

    // Check user limit
    if (channel.userLimit > 0 && channel.clients.size() >= static_cast<size_t>(channel.userLimit)) {
        return "Cannot join channel " + channelName + ": User limit reached.";
    }

    // Check channel key (password)
    if (!channel.key.empty() && password != channel.key) {
        return "Cannot join channel " + channelName + ": Incorrect password.";
    }

    return ""; // All checks passed
}


void handleJoin(int clientSockfd, const std::string& channelName, const std::string& password) {
    // Check if the channel name is valid
    if (channelName.empty() || channelName[0] != '#' || channelName.length() > 50) {
        send(clientSockfd, "Error: Invalid channel name. It must start with # and be 1-50 characters long.\n", 82, 0);
        return;
    }

    // Check if user exists; if not, create a new user
    if (users.find(clientSockfd) == users.end()) {
        User newUser;
        newUser.sockfd = clientSockfd;
        std::ostringstream oss;
        oss << "User" << clientSockfd;
        newUser.nickname = oss.str(); // Temporary nickname
        users[clientSockfd] = newUser;
    }

    User& user = users[clientSockfd];

    // Check if user is already in the channel
    if (std::find(user.channels.begin(), user.channels.end(), channelName) != user.channels.end()) {
        std::string response = "You are already in channel " + channelName + ".\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
        return;
    }

    // Check if the user can join the channel
    std::string errorMsg = canJoinChannel(clientSockfd, channelName, password);
    if (!errorMsg.empty()) {
        send(clientSockfd, errorMsg.c_str(), errorMsg.length(), 0);
        return;
    }

    // If all checks pass, proceed to join the channel
    std::map<std::string, Channel>::iterator it = channels.find(channelName);
    
    if (it == channels.end()) {
        // Channel doesn't exist; create it
        Channel newChannel;
        newChannel.name = channelName;
        newChannel.clients.push_back(clientSockfd);
        newChannel.operators.push_back(clientSockfd); // Make the first user an operator
        newChannel.inviteOnly = false; // Initialize invite-only mode
        newChannel.userLimit = 0; // No limit by default
        newChannel.key = ""; // No key by default
        newChannel.topic = ""; // No topic by default
        channels[channelName] = newChannel;

        std::string response = "Channel " + channelName + " created. You are now an operator.\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
    } else {
        // Channel exists; add the client to it
        Channel& channel = it->second;

        // Add the client to the channel's client list
        channel.clients.push_back(clientSockfd);

        std::string response = "Joined channel " + channelName + ".\n";
        send(clientSockfd, response.c_str(), response.length(), 0);

        // Notify other clients in the channel about the new member
        std::ostringstream oss;
        oss << "User " << user.nickname << " has joined the channel.\n";
        
        for (std::vector<int>::iterator clientIt = channel.clients.begin(); clientIt != channel.clients.end(); ++clientIt) {
            send(*clientIt, oss.str().c_str(), oss.str().length(), 0);
        }
    }

    // Add the channel to the user's list of channels
    user.channels.push_back(channelName);
}

void handlePart(int clientSockfd, const std::string& channelName) {
    if (users.find(clientSockfd) == users.end() || channels.find(channelName) == channels.end()) {
        std::string response = "Error: Channel or user not found.\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
        return;
    }

    User& user = users[clientSockfd];
    Channel& channel = channels[channelName];

    // Remove user from channel
    channel.clients.erase(std::remove(channel.clients.begin(), channel.clients.end(), clientSockfd), channel.clients.end());
    channel.operators.erase(std::remove(channel.operators.begin(), channel.operators.end(), clientSockfd), channel.operators.end());

    // Remove channel from user's list
    user.channels.erase(std::remove(user.channels.begin(), user.channels.end(), channelName), user.channels.end());

    std::string response = "Left channel " + channelName + ".\n";
    send(clientSockfd, response.c_str(), response.length(), 0);

    // Notify other users in the channel
    std::string notification = "User " + user.nickname + " has left the channel.\n";
    for (std::vector<int>::const_iterator it = channel.clients.begin(); it != channel.clients.end(); ++it) {
        send(*it, notification.c_str(), notification.length(), 0);
    }

    // If channel is empty, remove it
    if (channel.clients.empty()) {
        channels.erase(channelName);
    }
}

void processMessage(const std::string& message, int clientSockfd) {
    std::string command;
    std::string args;

    size_t spacePos = message.find(' ');
    if (spacePos != std::string::npos) {
        command = message.substr(0, spacePos);
        args = message.substr(spacePos + 1);
    } else {
        command = message;
    }

    // Global commands
    if (command == "/nick") {
        if (!args.empty()) {
            handleNick(clientSockfd, args);
        } else {
            send(clientSockfd, "Error: Missing nickname for NICK command.\n", 44, 0);
        }
    }
    else if (command == "/privmsg") {
        size_t spacePos = args.find(' ');
        if (spacePos != std::string::npos) {
            std::string target = args.substr(0, spacePos);
            std::string msg = args.substr(spacePos + 1);
            if (!target.empty() && !msg.empty()) {
                handlePrivMsg(clientSockfd, target, msg);
            } else {
                send(clientSockfd, "Error: Missing arguments for PRIVMSG command.\n", 47, 0);
            }
        } else {
            send(clientSockfd, "Error: Missing arguments for PRIVMSG command.\n", 47, 0);
        }
    }
    else if (command == "/join") {
        // Split args into channel name and optional password
        size_t spacePos = args.find(' ');
        std::string channelName;
        std::string password;

        if (spacePos != std::string::npos) {
            channelName = args.substr(0, spacePos);
            password = args.substr(spacePos + 1); // Get password after the first space
        } else {
            channelName = args; // No password provided
        }

        handleJoin(clientSockfd, channelName, password); // Pass both channel name and password
    }
    else if (command == "/topic") {
                // Parse the TOPIC command
                size_t spacePos = args.find(' ');
                std::string channelName;
                std::string topic;

                if (spacePos != std::string::npos) {
                    channelName = args.substr(0, spacePos);
                    topic = args.substr(spacePos + 1); // Get the new topic if provided
                } else {
                    channelName = args; // Only channel name provided, no new topic
                }

                if (!channelName.empty()) {
                    handleTopic(clientSockfd, channelName, topic); // Call your handleTopic function
                } else {
                    send(clientSockfd, "Error: Missing channel name for TOPIC command.\n", 48, 0);
                }
    }
    // Channel commands
    else if (command == "/kick" || command == "/invite" || command == "/topic" || command == "/mode") {
        size_t spacePos = args.find(' ');
        if (spacePos != std::string::npos) {
            std::string channelName = args.substr(0, spacePos);
            std::string channelArgs = args.substr(spacePos + 1);
            
            if (command == "/kick") {
                handleKick(clientSockfd, channelName, channelArgs);
            }
            else if (command == "/invite") {
                handleInvite(clientSockfd, channelName, channelArgs);
            }
            else if (command == "/mode") {
                size_t modeSpacePos = channelArgs.find(' ');
                std::string mode = channelArgs.substr(0, modeSpacePos);
                std::string modeArgs = (modeSpacePos != std::string::npos) ? channelArgs.substr(modeSpacePos + 1) : "";
                handleMode(clientSockfd, channelName, mode, modeArgs);
            }
        } else {
            std::string errorMsg = "Error: Missing arguments for " + command + " command.\n";
            send(clientSockfd, errorMsg.c_str(), errorMsg.length(), 0);
        }
    }
    else {
        std::string errorMsg = "Unknown command: " + command + "\n";
        send(clientSockfd, errorMsg.c_str(), errorMsg.length(), 0);
    }
}


void handlePrivMsg(int clientSockfd, const std::string& targetNick, const std::string& message) {
    int targetSockfd = findClientByNick(targetNick);
    if (targetSockfd == -1) {
        std::string response = "User not found.\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
        return;
    }

    std::string senderNick = clientNicks[clientSockfd];
    std::string fullMessage = "Private message from " + senderNick + ": " + message + "\n";
    send(targetSockfd, fullMessage.c_str(), fullMessage.length(), 0);

    std::string confirmation = "Message sent to " + targetNick + "\n";
    send(clientSockfd, confirmation.c_str(), confirmation.length(), 0);
}

void removeClient(int clientSockfd) {
    // Remove the client's nickname from the maps
    std::string nick = clientNicks[clientSockfd];
    clientNicks.erase(clientSockfd);
    nickToFd.erase(nick);

    // Remove the client from all channels
    for (std::map<std::string, Channel>::iterator it = channels.begin(); it != channels.end(); ++it) {
        std::vector<int>& clients = it->second.clients;
        clients.erase(std::remove(clients.begin(), clients.end(), clientSockfd), clients.end());
    }
}
