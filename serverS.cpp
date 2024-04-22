/*
 * serverS.cpp
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

#define SERVER_S_PORT "41705"
#define SERVER_M_UDP_PORT 44705
#define HOST_NAME "127.0.0.1"
#define MAXBUFLEN 1000

int main() {
   std::string filename = "single.txt";
   std::ifstream file(filename);
   std::map<std::string, int> roomMap;
   std::string line;

   while (getline(file, line)) {
      std::istringstream iss(line);
      std::string roomNumber;
      int count;
      if (!(iss >> roomNumber >> count)) {
         std::cerr << "file error: " << line << std::endl;
         continue;
      }
      roomNumber.pop_back();
      roomMap[roomNumber] = count;
   }
   file.close();

   // refers to Beej's guide
   int sockfd;
   struct addrinfo hints, *servinfo, *p;
   int rv;

   int yes = 1;
   int numbytes;
   struct sockaddr_storage their_addr;
   char buf[MAXBUFLEN];
   socklen_t addr_len;

   memset(&hints, 0, sizeof hints);
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;

   if ((rv = getaddrinfo(HOST_NAME, SERVER_S_PORT, &hints, &servinfo)) != 0) {
      fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
      return 1;
   }

   // loop through all the results and make a socket
   for (p = servinfo; p != NULL; p = p->ai_next) {
      if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
         perror("talker: socket");
         continue;
      }
      if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
         perror("setsockopt");
         exit(1);
      }
      if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
         close(sockfd);
         perror("server: bind");
         continue;
      }
      break;
   }

   if (p == NULL) {
      fprintf(stderr, "talker: failed to create socket\n");
      return 2;
   }
   std::cout << "The Server S is up and running using UDP on port " << SERVER_S_PORT << std::endl;

   std::string message;
   for (const auto &pair : roomMap) {
      // Serialize the key
      uint16_t keySize = htons(pair.first.size());
      message.append(reinterpret_cast<const char *>(&keySize), sizeof(uint16_t));
      message.append(pair.first);
      // Serialize the value
      int value = htonl(pair.second);
      message.append(reinterpret_cast<const char *>(&value), sizeof(int));
   }
   const char *msg = message.c_str();

   struct sockaddr_in M_addr;
   M_addr.sin_family = AF_INET;
   M_addr.sin_port = htons(SERVER_M_UDP_PORT);
   inet_aton(HOST_NAME, &M_addr.sin_addr);

   if ((numbytes = sendto(sockfd, msg, message.size(), 0, (sockaddr *)&M_addr, sizeof(M_addr))) == -1) {
      perror("talker: sendto");
      exit(1);
   }
   std::cout << "The Server S has sent the room status to the main server." << std::endl;

   freeaddrinfo(servinfo);

   // processing requests from the main server
   while (1) {
       if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
           perror("recvfrom");
           exit(1);
       }
       buf[numbytes] = '\0';
       std::string receivedData(buf);
       // Extracting the action, roomCode, and username from the received message
       // Recieved message format is "Action RoomCode Username"
       size_t firstSpacePos = receivedData.find(' ');
       size_t secondSpacePos = receivedData.find(' ', firstSpacePos + 1);
       std::string action = receivedData.substr(0, firstSpacePos);
       std::string roomCode = receivedData.substr(firstSpacePos + 1, secondSpacePos - firstSpacePos - 1);
       std::string username = receivedData.substr(secondSpacePos + 1);
       // std::cout << "Action: " << action << std::endl;
       // std::cout << "Room Code: " << roomCode << std::endl;
       // std::cout << "Username: " << username << std::endl;

       if (action == "Reservation") {
           std::cout << "The Server S received a reservation request from the main server." << std::endl;
       } else if (action == "Availability") {
           std::cout << "The Server S received an availability request from the main server." << std::endl;
       }

      int aftRoomCount;
      int preRoomCount;
      if (action == "Reservation") {
          if (roomMap.find(roomCode) != roomMap.end()) {
              preRoomCount = roomMap[roomCode];
              if (roomMap[roomCode] > 0) {
                  roomMap[roomCode] -= 1;
                  std::cout << "Successful reservation. The count of Room " << roomCode << " is now " << roomMap[roomCode] << "." << std::endl;
              } else {
                  std::cout << "Cannot make a reservation. Room " << roomCode << " is not available." << std::endl;
              }
              aftRoomCount = roomMap[roomCode];
          } else {
              std::cout << "Cannot make a reservation. Not able to find the room layout." << std::endl;
              aftRoomCount = -1;
          }
      } else if (action == "Availability") {
          if (roomMap.find(roomCode) != roomMap.end()) {
              preRoomCount = roomMap[roomCode];
              if (roomMap[roomCode] > 0) {
                  std::cout << "Room " << roomCode << " is available." << std::endl;
              } else {
                  std::cout << "Room " << roomCode << " is not available." << std::endl;
              }
              aftRoomCount = roomMap[roomCode];
          } else {
              std::cout << "Not able to find the room layout." << std::endl;
              aftRoomCount = -1;
          }
      }

      // Generate the message for UDP response
      // format example "Availability 2 2"
      // format example "Reservation 5 4"
      // format example "Reservation 0 0"
      std::string responseMessage = action + " " + std::to_string(preRoomCount) + " " + std::to_string(aftRoomCount);
      const char* responseData = responseMessage.c_str();
      size_t responseDataLength = responseMessage.size();
      // std::cout << "responseMessage: " << responseMessage << std::endl;

      // Sending the response
      if ((numbytes = sendto(sockfd, responseData, responseDataLength, 0, (sockaddr *)&M_addr, sizeof(M_addr))) == -1) {
          perror("talker: sendto");
          exit(1);
      }

      if (preRoomCount != aftRoomCount) {
          std::cout << "The Server S finished sending the response and the updated room status to the main server." << std::endl;
      } else {
          std::cout << "The Server S finished sending the response to the main server." << std::endl;
      }
   }

   close(sockfd);
   return 0;
}

