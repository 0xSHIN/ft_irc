#include "Channel.hpp"
#include "Commands.hpp"
#include "Kek.hpp"
#include <sys/socket.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <poll.h>
#include <map>

#define BUFFER_SIZE 1024

std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \r\n");
    size_t last = str.find_last_not_of(" \r\n");
    return (first == std::string::npos || last == std::string::npos) ? "" : str.substr(first, last - first + 1);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
        return 1;
    }

    int port = std::atoi(argv[1]);
    std::string password = argv[2];

    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "Error creating socket" << std::endl;
        return 1;
    }

    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error binding socket" << std::endl;
        return 1;
    }

    if (listen(serverSock, 5) < 0) {
        std::cerr << "Error listening on socket" << std::endl;
        return 1;
    }

    std::vector<pollfd> fds(1);
    fds[0].fd = serverSock;
    fds[0].events = POLLIN;

    std::map<int, Client> clients;

    while (true) {
        int activity = poll(&fds[0], fds.size(), -1);
        if (activity < 0) {
            std::cerr << "Poll error" << std::endl;
            break;
        }

        if (fds[0].revents & POLLIN) {
            int clientSock = accept(serverSock, NULL, NULL);
            if (clientSock < 0) {
                std::cerr << "Error accepting connection" << std::endl;
                continue;
            }

            pollfd newPollFd;
            newPollFd.fd = clientSock;
            newPollFd.events = POLLIN;
            fds.push_back(newPollFd);

            clients[clientSock] = Client();
            clients[clientSock].fd = clientSock;

            send(clientSock, "Connect using PASS [password]:\n", 31, 0);
        }

        for (size_t i = 1; i < fds.size(); i++) {
            if (fds[i].revents & POLLIN) {
                char buffer[BUFFER_SIZE];
                int bytesRead = recv(fds[i].fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytesRead <= 0) {
                    close(fds[i].fd);
                    clients.erase(fds[i].fd);
                    fds.erase(fds.begin() + i);
                    i--;
                    continue;
                }

                buffer[bytesRead] = '\0';
                clients[fds[i].fd].buffer += buffer;

                size_t pos;
                while ((pos = clients[fds[i].fd].buffer.find('\n')) != std::string::npos) {
                    std::string message = clients[fds[i].fd].buffer.substr(0, pos);
                    message = trim(message);
                    clients[fds[i].fd].buffer.erase(0, pos + 1);

                    if (clients[fds[i].fd].authenticated)
                    {
                        processMessage(message, fds[i].fd);
                        continue;
                    }

                    if (message.substr(0, 3) == "CAP") {
                        if (message.find("LS") != std::string::npos) {
                            send(fds[i].fd, "CAP * LS\r\n", 10, 0); // No capabilities
                        } else if (message.find("REQ") != std::string::npos) {
                            // Handle capability requests as needed
                        } else if (message.find("END") != std::string::npos) {
                            send(fds[i].fd, "CAP * ACK\r\n", 11, 0);
                        }
                        continue;
                    }

                    if (!clients[fds[i].fd].passwordVerified) {
                        if (message.substr(0, 4) == "PASS") {
                            std::string clientPassword = message.substr(5);
                            std::cout << "Received pass: '" << clientPassword << "'" << std::endl; // Debugging output
                            if (clientPassword == password) {
                                clients[fds[i].fd].passwordVerified = true;
                                send(fds[i].fd, "Authentication successful.\r\n", 28, 0);
                            } else {
                                send(fds[i].fd, "464 :Password incorrect\r\n", 25, 0);
                                continue;
                            }
                        }
                    }

                    if (clients[fds[i].fd].passwordVerified) {
                        if (message.substr(0, 4) == "NICK") {
                            handleNick(fds[i].fd, message.substr(5));
                            clients[fds[i].fd].nickname = message.substr(5);
                            clients[fds[i].fd].nickReceived = true;
                        }

                        if (message.substr(0, 4) == "USER") {
                            // Parse USER command
                            std::istringstream iss(message.substr(5));
                            std::string username, hostname, servername, realname;
                            iss >> username >> hostname >> servername;
                            std::getline(iss, realname);
                            realname = trim(realname);

                            if (username.empty() || realname.empty()) {
                                send(fds[i].fd, "461 USER :Not enough parameters\r\n", 35, 0);
                                continue;
                            }

                            clients[fds[i].fd].username = username;
                            clients[fds[i].fd].hostname = hostname;
                            clients[fds[i].fd].servername = servername;
                            clients[fds[i].fd].realname = realname;
                            clients[fds[i].fd].userReceived = true;
                        }

                        // After both NICK and USER, authenticate the user
                        if (clients[fds[i].fd].nickReceived && clients[fds[i].fd].userReceived) {
                            clients[fds[i].fd].authenticated = true;

                            std::string welcomeMsg = std::string(":") + "irc.localhost" + " 001 " + clients[fds[i].fd].nickname +
                                                    " :Welcome to the IRC Network, " + clients[fds[i].fd].nickname + "\r\n";
                            send(fds[i].fd, welcomeMsg.c_str(), welcomeMsg.length(), 0);
                            std::cout << "Received msg: '" << message << "'" << std::endl; // Debugging output
                        }
                    }
                }
            }
        }
    }

    for (size_t i = 0; i < fds.size(); i++) {
        close(fds[i].fd);
    }

    return 0;
}
