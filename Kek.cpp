#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <poll.h>
#include <string>
#include <map>

#include "Commands.hpp"
#include "Channel.hpp"

#define MAX_CLIENTS 1024
#define BUFFER_SIZE 1024

struct Client {
    int fd;
    bool authenticated;
    std::string buffer;
    Client() : fd(-1), authenticated(false) {}
};

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <port> <password>" << std::endl;
        return 1;
    }

    int port = atoi(argv[1]);
    std::string password = argv[2];

    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "Error creating socket" << std::endl;
        return 1;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
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

            send(clientSock, "Enter password:\n", 16, 0);
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
                    clients[fds[i].fd].buffer.erase(0, pos + 1);

                    if (!clients[fds[i].fd].authenticated) {
                        if (message == password) {
                            clients[fds[i].fd].authenticated = true;
                            send(fds[i].fd, "Authentication successful.\n", 27, 0);
                        } else {
                            send(fds[i].fd, "Authentication failed. Try again:\n", 34, 0);
                        }
                    } else {
                        std::cout << "Message from client " << fds[i].fd << ": " << message << std::endl;
                        processMessage(message, fds[i].fd);
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
