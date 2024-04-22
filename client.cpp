/*
 * client.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <sstream>

#define SERVER_M_TCP_PORT "45705"
#define HOST_NAME "127.0.0.1"
#define MAXDATASIZE 100

// refers to Beej's guide
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// encrypt the username and password
std::string encrypt(const std::string &text) {
    std::string encryptedText = "";
    for (char c : text) {
        if (isalpha(c)) {
            char base = islower(c) ? 'a' : 'A';
            encryptedText += static_cast<char>((c - base + 3) % 26 + base);
        } else if (isdigit(c)) {
            encryptedText += static_cast<char>((c - '0' + 3) % 10 + '0');
        } else {
            encryptedText += c;  // Special characters remain unchanged
        }
    }
    return encryptedText;
}

int main() {

    int sockfd, numbytes;
    char buf[MAXDATASIZE], roomBuf[MAXDATASIZE];
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(HOST_NAME, SERVER_M_TCP_PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and connect to the first
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("client: connect");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return 2;
    }

    std::cout << "Client is up and running." << std::endl;

    freeaddrinfo(servinfo);

    // get the dynamic port
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    memset(&local_addr, 0, sizeof(local_addr));
    if (getsockname(sockfd, (struct sockaddr *)&local_addr, &addr_len) == -1) {
        perror("getsockname");
        exit(1);
    }

    while (true) {
        std::string username;
        std::string password;
        std::cout << "Please enter the username: ";
        getline(std::cin, username);
        std::cout << "Please enter the password: ";
        getline(std::cin, password);
        std::string message = encrypt(username) + " " + encrypt(password); // encrypt the username and password with a single space between them

        const char *msg = message.c_str();
        int len = strlen(msg);
        if (send(sockfd, msg, len, 0) == -1) {
            perror("send");
        }
        std::cout << username << " sent an authentication request to the main server." << std::endl;

        if ((numbytes = recv(sockfd, buf, MAXDATASIZE - 1, 0)) == -1) {
            perror("recv");
            exit(1);
        }
        buf[numbytes] = '\0';

        if (buf[0] == 's') { // s for success
            std::cout << "Welcome member " << username << "!" << std::endl;

            while (true) {
                // Asking user to choose between Availability check or Reservation
                std::string action;
                while (true) {  // ensure only valid actions are accepted
                    std::cout << "Would you like to search for the availability or make a reservation? ";
                    std::cout << "(Enter \"Availability\" to search for the availability or Enter \"Reservation\" to make a reservation): ";
                    getline(std::cin, action);
                    if (action == "Reservation" || action == "Availability") {
                        break;  // Valid action, break the validation loop
                    } else {
                        std::cout << "Invalid response: please enter \"Availability\" or \"Reservation\"." << std::endl;
                    }
                }

                std::string roomCode;
                std::cout << "Please enter the room code: ";
                getline(std::cin, roomCode);

                // Concatenate action and roomCode with a space
                std::string query = action + " " + roomCode;
                const char *queryMsg = query.c_str();
                int queryLen = strlen(queryMsg);

                if (send(sockfd, queryMsg, queryLen, 0) == -1) {
                    perror("send");
                }
                if (action == "Reservation") {
                    std::cout << username << " sent a reservation request to the main server." << std::endl;
                } else if (action == "Availability") {
                    std::cout << username << " sent an availability request to the main server." << std::endl;
                }

                if ((numbytes = recv(sockfd, roomBuf, MAXDATASIZE - 1, 0)) == -1) {
                    perror("recv");
                    exit(1);
                }
                roomBuf[numbytes] = '\0';
                std::cout << "The client received the response from the main server using TCP over port " << ntohs(local_addr.sin_port) << "." << std::endl;
                // Recieved message format is "Action preRoomCount aftRoomCount" ex. "Availability 2 2"
                std::string strRoomBuf(roomBuf);
                size_t firstSpacePos = strRoomBuf.find(' ');
                size_t secondSpacePos = strRoomBuf.find(' ', firstSpacePos + 1);
                int preRoomCount = std::stoi(strRoomBuf.substr(firstSpacePos + 1, secondSpacePos - firstSpacePos - 1));
                int aftRoomCount = std::stoi(strRoomBuf.substr(secondSpacePos + 1));

                if (action == "Reservation") {
                    if (aftRoomCount == -1) {
                        std::cout << "Oops! Not able to find the room layout." << std::endl;
                    } else if (aftRoomCount < preRoomCount) {
                        std::cout << "Congratulations! The reservation for Room " << query << " has been made." << std::endl;
                    } else if (preRoomCount == 0) {
                        std::cout << "Sorry! The requested room is not available." << std::endl;
                    }
                } else if (action == "Availability") {
                    if (aftRoomCount == -1) {
                        std::cout << "Not able to find the room layout." << std::endl;
                    } else if (preRoomCount > 0) {
                        std::cout << "The requested room is available." << std::endl;
                    } else if (preRoomCount == 0) {
                        std::cout << "The requested room is not available." << std::endl;
                    }
                }
                std::cout << " " << std::endl;
                std::cout << "-----Start a new request-----" << std::endl;
            }
        } else if (buf[0] == 'n') { // n for not found
            std::cout << "Failed login: Username does not exist." << std::endl;
        } else if (buf[0] == 'p') { // p for password not match
            std::cout << "Failed login: Password does not match." << std::endl;
        }
    }

    close(sockfd);
    return 0;
}