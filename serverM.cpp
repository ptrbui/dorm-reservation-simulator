/*
 * serverM.cpp
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
#include <signal.h>

#define SERVER_S_PORT 41705
#define SERVER_D_PORT 42705
#define SERVER_U_PORT 43705
#define SERVER_M_UDP_PORT "44705"
#define SERVER_M_TCP_PORT "45705"
#define HOST_NAME "127.0.0.1"
#define MAXBUFLEN 1000
#define BACKLOG 10

// refers to Beej's guide
void sigchld_handler(int s) {
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// decrypt
std::string decrypt(const std::string &text) {
    std::string decryptedText = "";
    for (char c : text) {
        if (isalpha(c)) {
            char base = islower(c) ? 'a' : 'A';
            decryptedText += static_cast<char>((c - base - 3 + 26) % 26 + base);
        } else if (isdigit(c)) {
            decryptedText += static_cast<char>((c - '0' - 3 + 10) % 10 + '0');
        } else {
            decryptedText += c;
        }
    }
    return decryptedText;
}


int main() {

    // Seting up UDP socket
    int sockfdUDP;
    struct addrinfo hintsUDP, *servinfoUDP, *pUDP;
    int rvUDP;
    int numbytesUDP;

    struct sockaddr_storage their_addrUDP;
    char bufUDP[MAXBUFLEN];
    socklen_t addr_lenUDP;

    memset(&hintsUDP, 0, sizeof hintsUDP);
    hintsUDP.ai_family = AF_INET;
    hintsUDP.ai_socktype = SOCK_DGRAM;

    if ((rvUDP = getaddrinfo(HOST_NAME, SERVER_M_UDP_PORT, &hintsUDP, &servinfoUDP)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rvUDP));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (pUDP = servinfoUDP; pUDP != NULL; pUDP = pUDP->ai_next) {
        if ((sockfdUDP = socket(pUDP->ai_family, pUDP->ai_socktype, pUDP->ai_protocol)) == -1){
            perror("listener: socket");
            continue;
        }
        if (bind(sockfdUDP, pUDP->ai_addr, pUDP->ai_addrlen) == -1) {
            close(sockfdUDP);
            perror("listener: bind");
            continue;
        }
        break;
    }

    if (pUDP == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        return 2;
    }
    freeaddrinfo(servinfoUDP);
    std::cout << "The main server is up and running." << std::endl;

    std::map<std::string, int> roomMap;
    for (int i = 0; i < 3; i++) {
        addr_lenUDP = sizeof their_addrUDP;
        if ((numbytesUDP = recvfrom(sockfdUDP, bufUDP, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addrUDP, &addr_lenUDP)) == -1) {
            perror("recvfrom");
            exit(1);
        }
        bufUDP[numbytesUDP] = '\0';
        struct sockaddr_in *src_addr = (struct sockaddr_in *)&their_addrUDP;
        unsigned short port = ntohs(src_addr->sin_port);
        char serverID;
        if (port == SERVER_S_PORT) serverID = 'S';
        else if (port == SERVER_D_PORT) serverID = 'D';
        else if (port == SERVER_U_PORT) serverID = 'U';
        std::cout << "The main server has received the room status from Server " << serverID << " using UDP over port " << SERVER_M_UDP_PORT << "." << std::endl;

        // Parse the binary message into a std::map
        size_t index = 0;
        while (index < static_cast<size_t>(numbytesUDP)) {
            // Extract the key size
            uint16_t keySize;
            memcpy(&keySize, bufUDP + index, sizeof(uint16_t));
            keySize = ntohs(keySize);
            index += sizeof(uint16_t);
            // Extract the key
            std::string key(bufUDP + index, keySize);
            index += keySize;
            // Extract the value
            int value;
            memcpy(&value, bufUDP + index, sizeof(int));
            value = ntohl(value);
            index += sizeof(int);
            // Insert the key-value pair into the std::map
            roomMap[key] = value;
        }
    }

    std::string filename = "member.txt";
    std::ifstream file(filename);
    std::map<std::string, std::string> memberMap;
    std::string line;
    while (getline(file, line)) {
        std::istringstream iss(line);
        std::string username;
        std::string password;
        if (!(iss >> username >> password)) {
            std::cerr << "file error: " << line << std::endl;
            continue;
        }
        username.pop_back();
        memberMap[username] = password;
    }
    file.close();

    // setting up TCP server socket
    int sockfdTCP, new_fd;
    int numbytesTCP;
    struct addrinfo hintsTCP, *servinfoTCP, *pTCP;
    struct sockaddr_storage their_addrTCP;
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    int rvTCP;
    char bufTCP[MAXBUFLEN];

    memset(&hintsTCP, 0, sizeof hintsTCP);
    hintsTCP.ai_family = AF_UNSPEC;
    hintsTCP.ai_socktype = SOCK_STREAM;
    hintsTCP.ai_flags = AI_PASSIVE; //??????

    if ((rvTCP = getaddrinfo(NULL, SERVER_M_TCP_PORT, &hintsTCP, &servinfoTCP)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rvTCP));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (pTCP = servinfoTCP; pTCP != NULL; pTCP = pTCP->ai_next) {
        if ((sockfdTCP = socket(pTCP->ai_family, pTCP->ai_socktype, pTCP->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        if (setsockopt(sockfdTCP, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
        if (bind(sockfdTCP, pTCP->ai_addr, pTCP->ai_addrlen) == -1) {
            close(sockfdTCP);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(servinfoTCP); // all done with this structure

    if (pTCP == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfdTCP, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    // deal with all dead processes
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // accept() loop
    while (true) {
        sin_size = sizeof their_addrTCP;
        new_fd = accept(sockfdTCP, (struct sockaddr *)&their_addrTCP, &sin_size);
        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        if (!fork()) { // this is the child process
            close(sockfdTCP); // child doesn't need the listener
            // receiving Login and confirmation requests from client
            while (true) {
                numbytesTCP = recv(new_fd, bufTCP, MAXBUFLEN - 1, 0);
                if (numbytesTCP == -1) {
                    perror("recv");
                    exit(1);
                } else if (numbytesTCP == 0) {
                    break; // connection closed by client
                }
                bufTCP[numbytesTCP] = '\0';


                std::string received = std::string(bufTCP);
                size_t delim_pos = received.find(" ");
                std::string username = received.substr(0, delim_pos);
                std::string password = received.substr(delim_pos + 1);
                std::string response;
                std::cout << "The main server received the authentication for " << decrypt(username) << " using TCP over port " << SERVER_M_TCP_PORT << "." << std::endl;

                // s for success, n for not found, p for password not match
                if (memberMap.find(username) != memberMap.end()) {
                    if (memberMap[username] == password) {
                        response = "s";
                        if (send(new_fd, response.c_str(), response.size(), 0) == -1) {
                            perror("send");
                        }
                        std::cout << "The main server sent the authentication result to the client." << std::endl;

                        // Listen for room reservation or availability requests
                        while (true) {
                            if ((numbytesTCP = recv(new_fd, bufTCP, MAXBUFLEN - 1, 0)) == -1) {
                                perror("recv");
                                exit(1);
                            }
                            bufTCP[numbytesTCP] = '\0';

                            // Parsing action and roomCode
                            std::string receivedRequest(bufTCP);
                            size_t first_space = receivedRequest.find(" ");
                            std::string action = receivedRequest.substr(0, first_space);
                            std::string roomCode = receivedRequest.substr(first_space + 1);
                            if (action == "Reservation") {
                                std::cout << "The main server has received the reservation request on Room " << roomCode << " from " << decrypt(username) << " over port " << SERVER_M_TCP_PORT << "." << std::endl;
                            } else if (action == "Availability") {
                                std::cout << "The main server has received the availability request on Room " << roomCode << " from " << decrypt(username) << " over port " << SERVER_M_TCP_PORT << "." << std::endl;
                            }

                            char serverType = roomCode[0];
                            int udpPort;
                            bool sendStatus = false;

                            if (serverType == 'S') {
                                udpPort = SERVER_S_PORT;
                                sendStatus = true;
                            } else if (serverType == 'D') {
                                udpPort = SERVER_D_PORT;
                                sendStatus = true;
                            } else if (serverType == 'U') {
                                udpPort = SERVER_U_PORT;
                                sendStatus = true;
                            }

                            if (sendStatus) {
                                struct sockaddr_in M_addr;
                                M_addr.sin_family = AF_INET;
                                M_addr.sin_port = htons(udpPort);
                                inet_aton(HOST_NAME, &M_addr.sin_addr);
                                std::string udpMessage = std::string(bufTCP) + " " + username;
                                strcpy(bufTCP, udpMessage.c_str());
                                // std::cout << "udpMessage: " << udpMessage << std::endl;

                                if ((numbytesUDP = sendto(sockfdUDP, bufTCP, strlen(bufTCP), 0, (sockaddr *)&M_addr, sizeof(M_addr))) == -1) {
                                    perror("talker: sendto");
                                    exit(1);
                                }
                                std::cout << "The main server sent a request to Server " << serverType << "." << std::endl;

                                if ((numbytesUDP = recvfrom(sockfdUDP, bufUDP, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addrUDP, &addr_lenUDP)) == -1) {
                                    perror("recvfrom");
                                    exit(1);
                                }
                                bufUDP[numbytesUDP] = '\0';

                                // Recieved message format is "Action preRoomCount aftRoomCount" example "Availability 2 2"
                                std::string strBufUDP(bufUDP);  // Convert the received message to a std::string for processing.
                                size_t firstSpacePos = strBufUDP.find(' ');
                                size_t secondSpacePos = strBufUDP.find(' ', firstSpacePos + 1);
                                std::string action = strBufUDP.substr(0, firstSpacePos);
                                int preRoomCount = std::stoi(strBufUDP.substr(firstSpacePos + 1, secondSpacePos - firstSpacePos - 1));
                                int aftRoomCount = std::stoi(strBufUDP.substr(secondSpacePos + 1));

                                if (preRoomCount == aftRoomCount) { // no update
                                    std::cout << "The main server received the response from Server " << serverType << " using UDP over port " << SERVER_M_UDP_PORT <<  "." << std::endl;
                                } else { // update
                                    std::cout << "The main server received the response and the updated room status from Server " << serverType << " using UDP over port " << SERVER_M_UDP_PORT <<  "." << std::endl;
                                    std::cout << "The room status of " << roomCode << " has been updated." << std::endl;
                                }
                            } else {
                                // Setting preRoomCount and aftRoomCount to -1 when the roomType (S,D,U) is not found
                                int preRoomCount = -1;
                                int aftRoomCount = -1;
                                std::string response = action + " " + std::to_string(preRoomCount) + " " + std::to_string(aftRoomCount);
                                strncpy(bufUDP, response.c_str(), sizeof(bufUDP) - 1);
                                bufUDP[sizeof(bufUDP) - 1] = '\0';  // Ensure null termination
                            }

                            if (send(new_fd, bufUDP, strlen(bufUDP), 0) == -1) {
                                perror("send");
                            }
                            std::cout << "The main server sent the reservation result to the client." << std::endl;
                        }

                    } else { // password doesn't match - sending "p" reply to client
                        response = "p";
                        if (send(new_fd, response.c_str(), response.size(), 0) == -1) {
                            perror("send");
                        }
                    }
                } else { // username couldn't be found - sending "n" reply to client
                    response = "n";
                    if (send(new_fd, response.c_str(), response.size(), 0) == -1) {
                        perror("send");
                    }
                }
            }
            close(new_fd);
            exit(0); // child exits after handling client
        }
        close(new_fd);  // parent closes this socket as it's handled by child
    }
}