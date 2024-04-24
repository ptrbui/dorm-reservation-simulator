/*
 * Author: Peter Bui
 * Component: serverM.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
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
#define SERVER_M_UDP "44705"
#define SERVER_M_TCP "45705"
#define LOCALHOST "127.0.0.1"
#define MAXBUFLEN 1000
#define BACKLOG 10

// sigchld_handler from Beej's Guide
void sigchld_handler(int s) {
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}


// decrypt encrypted username and passwords from client
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

    // Setting up UDP Socket ~from Beej's Guide
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

    if ((rvUDP = getaddrinfo(LOCALHOST, SERVER_M_UDP, &hintsUDP, &servinfoUDP)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rvUDP));
        return 1;
    }

    // connecting to first available socket
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

        // *********************************************************************
        // ********** RECEIVE INITIAL ROOM STATUS FROM BACKEND SERVER **********
        // *********************************************************************
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
        std::cout << "The main server has received the room status from Server " << serverID << " using UDP over port " << SERVER_M_UDP << "." << std::endl;

        // parse binary msg into map
        size_t index = 0;
        while (index < static_cast<size_t>(numbytesUDP)) {
            // retrieving key size
            uint16_t keySize;
            memcpy(&keySize, bufUDP + index, sizeof(uint16_t));
            keySize = ntohs(keySize);
            index += sizeof(uint16_t);
            // retrieving key
            std::string key(bufUDP + index, keySize);
            index += keySize;
            // retrieving value
            int value;
            memcpy(&value, bufUDP + index, sizeof(int));
            value = ntohl(value);
            index += sizeof(int);
            // insert key-value into map
            roomMap[key] = value;
        }
    }

    std::string filename = "../data/member.txt";
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

    // Setting up TCP Socket ~from Beej's Guide
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
    hintsTCP.ai_flags = AI_PASSIVE;

    if ((rvTCP = getaddrinfo(NULL, SERVER_M_TCP, &hintsTCP, &servinfoTCP)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rvTCP));
        return 1;
    }

    // connecting to first available socket
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
    freeaddrinfo(servinfoTCP);

    if (pTCP == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfdTCP, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }
    // handle dead processes
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

        if (!fork()) {
            close(sockfdTCP);
            while (true) {

                // *****************************************************************
                // ********** RECEIVE AUTHENTICATION REQUEST FROM CLIENT ***********
                // *****************************************************************
                // RECEIVED FORMAT: "userType username password"
                numbytesTCP = recv(new_fd, bufTCP, MAXBUFLEN - 1, 0);
                if (numbytesTCP == -1) {
                    perror("recv");
                    exit(1);
                } else if (numbytesTCP == 0) {
                    break;
                }
                bufTCP[numbytesTCP] = '\0';
                std::string received = std::string(bufTCP);
                size_t firstSpacePos = received.find(' ');
                size_t secondSpacePos = received.find(' ', firstSpacePos + 1);
                std::string userType = received.substr(0, firstSpacePos);
                std::string username = received.substr(firstSpacePos + 1, secondSpacePos - firstSpacePos - 1);
                std::string password = received.substr(secondSpacePos + 1);
                std::string response;

                // APPROVED AUTHENTICATION "s"
                if (memberMap.find(username) != memberMap.end()) {
                    std::cout << "The main server received the authentication for " << decrypt(username) << " using TCP over port " << SERVER_M_TCP << "." << std::endl;
                    if (memberMap[username] == password) {
                        // *************************************************************
                        // ********** SEND APPROVED AUTHENTICATION TO CLIENT ***********
                        // *************************************************************
                        // SEND FORMAT: "response"
                        response = "s";
                        if (send(new_fd, response.c_str(), response.size(), 0) == -1) {
                            perror("send");
                        }
                        std::cout << "The main server sent the authentication result to the client." << std::endl;

                        while (true) {
                            // *********************************************************
                            // ********** RECEIVE ACTION REQUEST FROM CLIENT ***********
                            // *********************************************************
                            // RECEIVE FORMAT: "Action RoomCode"
                            if ((numbytesTCP = recv(new_fd, bufTCP, MAXBUFLEN - 1, 0)) == -1) {
                                perror("recv");
                                exit(1);
                            }
                            bufTCP[numbytesTCP] = '\0';
                            std::string receivedRequest(bufTCP);
                            size_t first_space = receivedRequest.find(" ");
                            std::string action = receivedRequest.substr(0, first_space);
                            std::string roomCode = receivedRequest.substr(first_space + 1);
                            if (action == "Reservation") {
                                std::cout << "The main server has received the reservation request on Room " << roomCode << " from " << decrypt(username) << " over port " << SERVER_M_TCP << "." << std::endl;
                            } else if (action == "Availability") {
                                std::cout << "The main server has received the availability request on Room " << roomCode << " from " << decrypt(username) << " over port " << SERVER_M_TCP << "." << std::endl;
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
                                inet_aton(LOCALHOST, &M_addr.sin_addr);
                                std::string udpMessage = std::string(bufTCP) + " " + username;
                                strcpy(bufTCP, udpMessage.c_str());

                                // ************************************************************************
                                // ********** SEND ACTION REQUEST FROM CLIENT TO BACKEND SERVER ***********
                                // ************************************************************************
                                // SEND FORMAT: "Action RoomCode username"
                                if ((numbytesUDP = sendto(sockfdUDP, bufTCP, strlen(bufTCP), 0, (sockaddr *)&M_addr, sizeof(M_addr))) == -1) {
                                    perror("talker: sendto");
                                    exit(1);
                                }
                                std::cout << "The main server sent a request to Server " << serverType << "." << std::endl;

                                // ******************************************************************
                                // ********** RECEIVE ACTION RESPONSE FROM BACKEND SERVER ***********
                                // ******************************************************************
                                // RECEIVE FORMAT: "Action preRoomCount aftRoomCount"
                                if ((numbytesUDP = recvfrom(sockfdUDP, bufUDP, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addrUDP, &addr_lenUDP)) == -1) {
                                    perror("recvfrom");
                                    exit(1);
                                }
                                bufUDP[numbytesUDP] = '\0';
                                std::string strBufUDP(bufUDP);
                                size_t firstSpacePos = strBufUDP.find(' ');
                                size_t secondSpacePos = strBufUDP.find(' ', firstSpacePos + 1);
                                std::string action = strBufUDP.substr(0, firstSpacePos);
                                int preRoomCount = std::stoi(strBufUDP.substr(firstSpacePos + 1, secondSpacePos - firstSpacePos - 1));
                                int aftRoomCount = std::stoi(strBufUDP.substr(secondSpacePos + 1));
                                if (preRoomCount == aftRoomCount) { // no update
                                    std::cout << "The main server received the response from Server " << serverType << " using UDP over port " << SERVER_M_UDP << "." << std::endl;
                                } else { // update
                                    std::cout << "The main server received the response and the updated room status from Server " << serverType << " using UDP over port " << SERVER_M_UDP << "." << std::endl;
                                    std::cout << "The room status of " << roomCode << " has been updated." << std::endl;
                                }
                            }

                            // If sendStatus is False, the roomType was not found. Set preRoomCount and aftRoomCount to -1.
                            else {
                                int preRoomCount = -1;
                                int aftRoomCount = -1;
                                std::string response = action + " " + std::to_string(preRoomCount) + " " + std::to_string(aftRoomCount);
                                strncpy(bufUDP, response.c_str(), sizeof(bufUDP) - 1);
                                bufUDP[sizeof(bufUDP) - 1] = '\0';  // Ensure null termination
                            }

                            // ***********************************************************************
                            // ********** SEND ACTION RESULT FROM BACKEND SERVER TO CLIENT ***********
                            // ***********************************************************************
                            // SEND FORMAT: "Action preRoomCount aftRoomCount"
                            if (send(new_fd, bufUDP, strlen(bufUDP), 0) == -1) {
                                perror("send");
                            }
                            if (action == "Reservation") {
                                std::cout << "The main server sent the reservation result to the client." << std::endl;
                            } else if (action == "Availability") {
                                std::cout << "The main server sent the availability information to the client." << std::endl;
                            }
                        }

                    }
                    // DENIED AUTHENTICATION "p"
                    else {
                        // ***********************************************************
                        // ********** SEND DENIED AUTHENTICATION TO CLIENT ***********
                        // ***********************************************************
                        // SEND FORMAT: "response"
                        response = "p";
                        if (send(new_fd, response.c_str(), response.size(), 0) == -1) {
                            perror("send");
                        }
                    }
                }

                else if (userType == "guest") {
                    std::cout << "The main server received the guest request for " << decrypt(username) << " using TCP over port " << SERVER_M_TCP << "." << std::endl;
                    std::cout << "The main server accepts " << decrypt(username) << " as a guest." << std::endl;
                        // *************************************************************
                        // ********** SEND APPROVED AUTHENTICATION TO GUEST ***********
                        // *************************************************************
                        // SEND FORMAT: "response"
                        response = "s";
                        if (send(new_fd, response.c_str(), response.size(), 0) == -1) {
                            perror("send");
                        }
                        std::cout << "The main server sent the guest result to the client." << std::endl;

                        while (true) {
                            // *********************************************************
                            // ********** RECEIVE ACTION REQUEST FROM CLIENT ***********
                            // *********************************************************
                            // RECEIVE FORMAT: "Action RoomCode"
                            if ((numbytesTCP = recv(new_fd, bufTCP, MAXBUFLEN - 1, 0)) == -1) {
                                perror("recv");
                                exit(1);
                            }
                            bufTCP[numbytesTCP] = '\0';
                            std::string receivedRequest(bufTCP);
                            size_t first_space = receivedRequest.find(" ");
                            std::string action = receivedRequest.substr(0, first_space);
                            std::string roomCode = receivedRequest.substr(first_space + 1);


                            if (action == "Availability") {
                                std::cout << "The main server has received the availability request on Room " << roomCode << " from " << decrypt(username) << " over port " << SERVER_M_TCP << "." << std::endl;
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
                                    inet_aton(LOCALHOST, &M_addr.sin_addr);
                                    std::string udpMessage = std::string(bufTCP) + " " + username;
                                    strcpy(bufTCP, udpMessage.c_str());

                                    // ******************************************************************************
                                    // ********** SEND AVAILABILITY REQUEST FROM CLIENT TO BACKEND SERVER ***********
                                    // ******************************************************************************
                                    // SEND FORMAT: "Action RoomCode username"
                                    if ((numbytesUDP = sendto(sockfdUDP, bufTCP, strlen(bufTCP), 0, (sockaddr *)&M_addr, sizeof(M_addr))) == -1) {
                                        perror("talker: sendto");
                                        exit(1);
                                    }
                                    std::cout << "The main server sent a request to Server " << serverType << "." << std::endl;

                                    // ************************************************************************
                                    // ********** RECEIVE AVAILABILITY RESPONSE FROM BACKEND SERVER ***********
                                    // ************************************************************************
                                    // RECEIVE FORMAT: "Action preRoomCount aftRoomCount"
                                    if ((numbytesUDP = recvfrom(sockfdUDP, bufUDP, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addrUDP, &addr_lenUDP)) == -1) {
                                        perror("recvfrom");
                                        exit(1);
                                    }
                                    bufUDP[numbytesUDP] = '\0';
                                    std::string strBufUDP(bufUDP);
                                    size_t firstSpacePos = strBufUDP.find(' ');
                                    size_t secondSpacePos = strBufUDP.find(' ', firstSpacePos + 1);
                                    std::string action = strBufUDP.substr(0, firstSpacePos);
                                    int preRoomCount = std::stoi(strBufUDP.substr(firstSpacePos + 1, secondSpacePos - firstSpacePos - 1));
                                    int aftRoomCount = std::stoi(strBufUDP.substr(secondSpacePos + 1));
                                    if (preRoomCount == aftRoomCount) { // no update
                                        std::cout << "The main server received the response from Server " << serverType << " using UDP over port " << SERVER_M_UDP << "." << std::endl;
                                    } else { // update
                                        std::cout << "The main server received the response and the updated room status from Server " << serverType << " using UDP over port " << SERVER_M_UDP << "." << std::endl;
                                        std::cout << "The room status of " << roomCode << " has been updated." << std::endl;
                                    }
                                } else { // If sendStatus is False, the roomType was not found. Set preRoomCount and aftRoomCount to -1.
                                    int preRoomCount = -1;
                                    int aftRoomCount = -1;
                                    std::string response = action + " " + std::to_string(preRoomCount) + " " + std::to_string(aftRoomCount);
                                    strncpy(bufUDP, response.c_str(), sizeof(bufUDP) - 1);
                                    bufUDP[sizeof(bufUDP) - 1] = '\0';
                                }
                            }

                            else if (action == "Reservation") {
                                std::cout << "The main server has received the reservation request on Room " << roomCode << " from " << decrypt(username) << " over port " << SERVER_M_TCP << "." << std::endl;
                                std::cout << decrypt(username) << " cannot make a reservation." << std::endl;

                                // if a guest tries to make a reservation, we set preRoomCount and aftRoomCount to -2
                                int preRoomCount = -2;
                                int aftRoomCount = -2;
                                std::string response = action + " " + std::to_string(preRoomCount) + " " + std::to_string(aftRoomCount);
                                strncpy(bufUDP, response.c_str(), sizeof(bufUDP) - 1);
                                bufUDP[sizeof(bufUDP) - 1] = '\0';
                            }

                            // *****************************************************************************
                            // ********** SEND ACTION RESULT FROM BACKEND SERVER TO CLIENT ***********
                            // *****************************************************************************
                            // SEND FORMAT: "Action preRoomCount aftRoomCount"
                            if (send(new_fd, bufUDP, strlen(bufUDP), 0) == -1) {
                                perror("send");
                            }
                            if (action == "Reservation") {
                                std::cout << "The main server sent the error message to the client." << std::endl;
                            } else if (action == "Availability") {
                                std::cout << "The main server sent the availability information to the client." << std::endl;
                            }
                        }
                }

                // UN-FOUND AUTHENTICATION "n"
                else {
                    // **********************************************************
                    // ********** SEND ERROR AUTHENTICATION TO CLIENT ***********
                    // **********************************************************
                    // SEND FORMAT: "response"
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