#include "Commands.hpp"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>
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
        sendMessage(clientSockfd, ":localhost 433 " + clients[clientSockfd].nickname + " " + newNick + " :Nickname already in use\r\n");
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
    clients[clientSockfd].nickname = newNick;

    // Send confirmation message
    sendMessage(clientSockfd, ":localhost 001 " + clients[clientSockfd].nickname + " :Nickname set to " + newNick + "\r\n");
}

bool isOperator(int clientSockfd) {
    return operators.find(clientSockfd) != operators.end();
}

void createChannel(int clientSockfd, const std::string& channelName) {
    // Create the new channel
    Channel newChannel;
    newChannel.name = channelName;
    newChannel.clients.push_back(clientSockfd);
    newChannel.operators.push_back(clientSockfd); // Make the first user an operator
    newChannel.inviteOnly = false; // Initialize invite-only mode
    newChannel.userLimit = 0; // No limit by default
    newChannel.key = ""; // No key by default
    newChannel.topic = ""; // No topic by default

    // Add the channel to the global channels map
    channels[channelName] = newChannel;

    // Send channel creation messages to the client
    std::string response = ":localhost 332 " + clients[clientSockfd].nickname + " " + channelName + " :" + newChannel.topic + "\r\n";
    sendMessage(clientSockfd, response.c_str());

    response = ":localhost 353 " + clients[clientSockfd].nickname + " = " + channelName + " :";
    std::ostringstream userList;
    userList << clients[clientSockfd].nickname << " ";
    response += userList.str();
    sendMessage(clientSockfd, response.c_str());

    // Notify the new user about their successful channel creation
    std::ostringstream oss;
    oss << ":" << clients[clientSockfd].nickname << "!" << clients[clientSockfd].nickname << "@localhost JOIN " << channelName << "\r\n";
    sendMessage(clientSockfd, oss.str().c_str());
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
            sendMessage(clientSockfd, ":localhost 473 " + clients[clientSockfd].nickname + " " + channelName + " :You must be invited to join this channel\r\n");
            return "error";  // Return empty to indicate an error was sent but no further processing needed
        }
    }

    // Check user limit
    if (channel.userLimit > 0 && channel.clients.size() >= static_cast<size_t>(channel.userLimit)) {
        sendMessage(clientSockfd, ":localhost 471 " + clients[clientSockfd].nickname + " " + channelName + " :Channel user limit reached\r\n");
        return "error";  // Return empty to indicate an error was sent but no further processing needed
    }

    // Check channel key (password)
    if (!channel.key.empty() && password != channel.key) {
        sendMessage(clientSockfd, ":localhost 475 " + clients[clientSockfd].nickname + " " + channelName + " :Incorrect channel password\r\n");
        return "error";  // Return empty to indicate an error was sent but no further processing needed
    }

    return ""; // All checks passed
}

void handleJoin(int clientSockfd, const std::string& channelName, const std::string& password) {
    // Check if the channel name is valid
    if (channelName.empty() || channelName[0] != '#' || channelName.length() > 50) {
        sendMessage(clientSockfd, ":localhost 403 " + clients[clientSockfd].nickname + " " + channelName + " :No such channel\r\n");
        return;
    }

    // Check if user exists; if not, create a new user
    if (clients.find(clientSockfd) == clients.end()) {
        Client newClient;
        newClient.fd = clientSockfd;
        std::ostringstream oss;
        oss << "User" << clientSockfd;
        newClient.nickname = oss.str(); // Temporary nickname
        clients[clientSockfd] = newClient;
    }

    Client& client = clients[clientSockfd];

    // Check if user is already in the channel
    if (std::find(client.channels.begin(), client.channels.end(), channelName) != client.channels.end()) {
        std::string response = ":localhost 443 " + clients[clientSockfd].nickname + " " + channelName + " :You are already in the channel\r\n";
        sendMessage(clientSockfd, response.c_str());
        return;
    }

    // Check if the user can join the channel
    std::string errorMsg = canJoinChannel(clientSockfd, channelName, password);
    if (!errorMsg.empty()) {
        sendMessage(clientSockfd, ":localhost 475 " + clients[clientSockfd].nickname + " " + channelName + " :Cannot join channel (+k or +i)\r\n");
        return;
    }

    // Check if the channel exists
    std::map<std::string, Channel>::iterator it = channels.find(channelName);

    if (it == channels.end()) {
        // If channel doesn't exist, call createChannel to create the channel
        createChannel(clientSockfd, channelName);
    } else {
        // Channel exists; add the client to it
        Channel& channel = it->second;

        // Add the client to the channel's client list
        channel.clients.push_back(clientSockfd);

        // Send channel join confirmation
        std::string response = ":localhost 353 " + clients[clientSockfd].nickname + " = " + channelName + " :";
        std::ostringstream userList;
        for (std::vector<int>::iterator it = channel.clients.begin(); it != channel.clients.end(); ++it) {
            userList << clients[*it].nickname << " ";
        }
        response += userList.str();
        send(clientSockfd, response.c_str(), response.length(), 0);

        // Notify other clients in the channel about the new member
        std::ostringstream oss;
        oss << ":" << clients[clientSockfd].nickname << "!" << clients[clientSockfd].nickname << "@localhost JOIN " << channelName << "\r\n";
        for (std::vector<int>::iterator clientIt = channel.clients.begin(); clientIt != channel.clients.end(); ++clientIt) {
            send(*clientIt, oss.str().c_str(), oss.str().length(), 0);
        }
    }

    // Add the channel to the user's list of channels
    client.channels.push_back(channelName);
}

void handlePart(int clientSockfd, const std::string& channelName) {
    if (clients.find(clientSockfd) == clients.end() || channels.find(channelName) == channels.end()) {
        std::string response = "Error: Channel or user not found.\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
        return;
    }

    Client& client = clients[clientSockfd];
    Channel& channel = channels[channelName];

    // Remove user from channel
    channel.clients.erase(std::remove(channel.clients.begin(), channel.clients.end(), clientSockfd), channel.clients.end());
    channel.operators.erase(std::remove(channel.operators.begin(), channel.operators.end(), clientSockfd), channel.operators.end());

    // Remove channel from user's list
    client.channels.erase(std::remove(client.channels.begin(), client.channels.end(), channelName), client.channels.end());

    std::string response = "Left channel " + channelName + ".\n";
    send(clientSockfd, response.c_str(), response.length(), 0);

    // Notify other users in the channel
    std::string notification = "User " + client.nickname + " has left the channel.\n";
    for (std::vector<int>::const_iterator it = channel.clients.begin(); it != channel.clients.end(); ++it) {
        send(*it, notification.c_str(), notification.length(), 0);
    }

    // If channel is empty, remove it
    if (channel.clients.empty()) {
        channels.erase(channelName);
    }
}

void handleChatMsg(int clientSockfd, const std::string& channelName, const std::string& msg) {
    // Find the channel
    std::map<std::string, Channel>::iterator it = channels.find(channelName);

    if (it != channels.end()) {
        Channel& channel = it->second;

        // Check if the client is part of the channel
        bool isClientInChannel = false;
        for (std::vector<int>::iterator itClient = channel.clients.begin(); itClient != channel.clients.end(); ++itClient) {
            if (*itClient == clientSockfd) {
                isClientInChannel = true;
                break;
            }
        }

        if (isClientInChannel) {
            // Send the message to all clients in the channel
            for (std::vector<int>::iterator itClient = channel.clients.begin(); itClient != channel.clients.end(); ++itClient) {
                if (*itClient != clientSockfd) { // Don't send the message to the sender
                    std::string response = ":" + clients[clientSockfd].nickname + " PRIVMSG " + channelName + " :" + msg + "\r\n";
                    send(*itClient, response.c_str(), response.length(), 0);
                }
            }

            // Optionally, send a confirmation back to the sender
            std::string response = "Message sent to channel " + channelName + ": " + msg + "\r\n";
            send(clientSockfd, response.c_str(), response.length(), 0);
        } else {
            // If the client is not part of the channel, notify them
            std::string response = ":localhost 442 " + clients[clientSockfd].nickname + " " + channelName + " :You're not on that channel\r\n";
            send(clientSockfd, response.c_str(), response.length(), 0);
        }
    } else {
        // If the channel doesn't exist, notify the sender
        std::string response = ":localhost 403 " + clients[clientSockfd].nickname + " " + channelName + " :No such channel\r\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
    }
}


void handleInvite(int clientSockfd, const std::string& channelName, const std::string& target) {
    // Find the channel
    std::map<std::string, Channel>::iterator it = channels.find(channelName);

    if (it != channels.end()) {
        Channel& channel = it->second;

        // Check if the client is an operator in the channel
        if (std::find(channel.operators.begin(), channel.operators.end(), clientSockfd) != channel.operators.end()) {
            // Find the target client
            std::map<std::string, int>::iterator targetIt = nickToFd.find(target);

            if (targetIt != nickToFd.end()) {
                int targetSockfd = targetIt->second;

                // Invite the client to the channel
                channel.invitedUsers.push_back(targetSockfd);

                // Send an invitation message to the target client
                std::string response = ":localhost 341 " + clients[clientSockfd].nickname + " " + target + " " + channelName + " :You have been invited to join the channel\r\n";
                send(targetSockfd, response.c_str(), response.length(), 0);

                // Optionally, send a confirmation back to the inviter
                response = ":localhost 341 " + clients[clientSockfd].nickname + " " + target + " " + channelName + " :Invitation sent\r\n";
                send(clientSockfd, response.c_str(), response.length(), 0);
            } else {
                // If the target client doesn't exist, notify the inviter
                std::string response = ":localhost 401 " + clients[clientSockfd].nickname + " " + target + " :No such nick\r\n";
                send(clientSockfd, response.c_str(), response.length(), 0);
            }
        } else {
            // If the client is not an operator in the channel, notify them
            std::string response = ":localhost 482 " + clients[clientSockfd].nickname + " " + channelName + " :You're not a channel operator\r\n";
            send(clientSockfd, response.c_str(), response.length(), 0);
        }
    } else {
        // If the channel doesn't exist, notify the inviter
        std::string response = ":localhost 403 " + clients[clientSockfd].nickname + " " + channelName + " :No such channel\r\n";
        send(clientSockfd, response.c_str(), response.length(), 0);
    }
}

void handlePrivMsg(int clientSockfd, const std::string& target, const std::string& msg) {
    // Check if the target is in the nickname-to-socket map
    std::map<std::string, int>::iterator it = nickToFd.find(target);

    if (it != nickToFd.end()) {
        int targetSockfd = it->second; // Get the socket ID of the target client

        // Send the private message to the target client
        std::string response = ":localhost 341 " + clients[clientSockfd].nickname + " PRIVMSG " + target + " :" + msg + "\r\n";
        send(targetSockfd, response.c_str(), response.length(), 0);

        // Optionally, send a confirmation back to the sender (this is how irssi works)
        std::string ackResponse = ":localhost 341 PRIVMSG " + clients[clientSockfd].nickname + " :" + msg + "\r\n";
        send(clientSockfd, ackResponse.c_str(), ackResponse.length(), 0);
    } else {
        // If the target client is not found, notify the sender with the "No such nick" error (401)
        std::string errorResponse = ":localhost 401 " + clients[clientSockfd].nickname + " " + target + " :No such nick\r\n";
        send(clientSockfd, errorResponse.c_str(), errorResponse.length(), 0);
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

    std::cout << message << " here is it" << std::endl;

    // Global commands
    if (command == "NICK") {
        if (!args.empty()) {
            handleNick(clientSockfd, args);
        } else {
            sendMessage(clientSockfd, ":localhost 431 " + clients[clientSockfd].nickname + " :No nickname given\r\n");
        }
    } else if (command == "PRIVMSG") {
    size_t spacePos = args.find(' ');
    if (spacePos != std::string::npos) {
        std::string target = args.substr(0, spacePos);
        std::string msg = args.substr(spacePos + 1);

        if (!target.empty() && !msg.empty()) {
            // Check if the target is a valid channel or user
            if (target[0] == '#') {
                // Target is a channel
                // Check if the channel exists
                std::map<std::string, Channel>::iterator it = channels.find(target);
                if (it != channels.end()) {
                    // Send message to all clients in the channel
                    handleChatMsg(clientSockfd, target, msg);
                } else {
                    sendMessage(clientSockfd, ":localhost 403 " + clients[clientSockfd].nickname + " " + target + " :No such channel\r\n");
                }
            } else {
                // Target is a user (find the user by nickname)
                std::map<std::string, int>::iterator nicknameIt = nickToFd.find(target);
                if (nicknameIt != nickToFd.end()) {
                    //int targetSockfd = nicknameIt->second; // Get the socket of the target user
                    handlePrivMsg(clientSockfd, nicknameIt->first, msg);
                } else {
                    sendMessage(clientSockfd, ":localhost 401 " + clients[clientSockfd].nickname + " " + target + " :No such nick\r\n");
                }
            }
        } else {
            sendMessage(clientSockfd, ":localhost 411 " + clients[clientSockfd].nickname + " :No recipient given (PRIVMSG)\r\n");
        }
    } else {
        sendMessage(clientSockfd, ":localhost 411 " + clients[clientSockfd].nickname + " :No recipient given (PRIVMSG)\r\n");
    }
} else if (command == "MSG") {
        size_t spacePos = args.find(' ');
        if (spacePos != std::string::npos) {
            std::string channelName = args.substr(0, spacePos);
            std::string msg = args.substr(spacePos + 1);
            if (!channelName.empty() && !msg.empty()) {
                handleChatMsg(clientSockfd, channelName, msg);
            } else {
                sendMessage(clientSockfd, ":localhost 461 " + clients[clientSockfd].nickname + " MSG :Not enough parameters\r\n");
            }
        } else {
            sendMessage(clientSockfd, ":localhost 461 " + clients[clientSockfd].nickname + " MSG :Not enough parameters\r\n");
        }
    } else if (command == "JOIN") {
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
    } else if (command == "TOPIC") {
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
            sendMessage(clientSockfd, ":localhost 461 " + clients[clientSockfd].nickname + " TOPIC :Not enough parameters\r\n");
        }
    }
    // Channel commands
    else if (command == "KICK" || command == "INVITE" || command == "TOPIC" || command == "MODE") {
        size_t spacePos = args.find(' ');
        if (spacePos != std::string::npos) {
            std::string channelName = args.substr(0, spacePos);
            std::string channelArgs = args.substr(spacePos + 1);

            if (command == "KICK") {
                handleKick(clientSockfd, channelName, channelArgs);
            } else if (command == "INVITE") {
                handleInvite(clientSockfd, channelName, channelArgs);
            } else if (command == "MODE") {
                size_t modeSpacePos = channelArgs.find(' ');
                std::string mode = channelArgs.substr(0, modeSpacePos);
                std::string modeArgs = (modeSpacePos != std::string::npos) ? channelArgs.substr(modeSpacePos + 1) : "";
                handleMode(clientSockfd, channelName, mode, modeArgs);
            }
        } else {
            std::string errorMsg = ":localhost 461 " + clients[clientSockfd].nickname + " " + command + " :Not enough parameters\r\n";
            sendMessage(clientSockfd, errorMsg.c_str());
        }
    } else {
        std::string errorMsg = ":localhost 421 " + clients[clientSockfd].nickname + " " + command + " :Unknown command\r\n";
        sendMessage(clientSockfd, errorMsg.c_str());
    }
}

void removeClient(int clientSockfd) {
    // Remove the client's nickname from the maps
    std::string nick = clients[clientSockfd].nickname;
    clients.erase(clientSockfd);
    nickToFd.erase(nick);

    // Remove the client from all channels
    for (std::map<std::string, Channel>::iterator it = channels.begin(); it != channels.end(); ++it) {
        std::vector<int>& clientsList = it->second.clients;
        clientsList.erase(std::remove(clientsList.begin(), clientsList.end(), clientSockfd), clientsList.end());
    }
}
