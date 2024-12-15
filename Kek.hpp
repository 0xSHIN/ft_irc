#ifndef KEK_HPP
#define KEK_HPP

#include <string>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>

class Client {
public:
    int fd;
    std::string nickname;
    std::string username;
    std::string hostname;
    std::string servername;
    std::string realname;
    std::vector<std::string> channels;
    bool authenticated;
    std::string buffer;
    bool nickReceived;
    bool userReceived;
    bool passwordVerified;

    Client() : fd(-1), authenticated(false), nickReceived(false), userReceived(false), passwordVerified(false) {}
};

#endif // KEK_HPP
