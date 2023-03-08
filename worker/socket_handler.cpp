#include <vector>
#include <string>
#include <iostream>
#include <cstring>

#include <unistd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "socket_handler.h"

SocketHandler::SocketHandler(std::vector<int> socket_thread_fds, int socket_processing_fd) {
	this->socket_thread_fds = socket_thread_fds;
	this->socket_processing_fd = socket_processing_fd;
}

void SocketHandler::socketThread() {
	std::cout << "Socket handler started." << std::endl;
	char buffer[1024] = { 0 };
	int readLength = 0;

	struct pollfd pfds[socket_thread_fds.size()];
	int status = 0;
	while(true) {
		for (int i = 0; i < socket_thread_fds.size(); i++) {
			pfds[i].fd = socket_thread_fds.at(i);
			pfds[i].events = POLLIN;
		}
		std::cout << "Socket thread polling..." << std::endl;
		status = poll(pfds, socket_thread_fds.size(), -1);
		if (status < 0) {
			std::cerr << "Error with poll (socket)." << std::endl;
			break;
		}
		if (status == 0) {
			std::cerr << "Poll returned 0 events? (socket)" << std::endl;
			break;
		}
		if (pfds[0].revents == POLLIN) {
			readLength = read(pfds[0].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Error reading from main pipe." << std::endl;
			} else {
				std::string pipeValue(buffer);
				std::cout << "Message output: " << pipeValue << std::endl;
				if (pipeValue == "exit") {
					std::cout << "Terminating socket thread." << std::endl;
					break;
				} else { //TEMPORARY PSEUDOMESSAGE HANDLER
					write(socket_processing_fd, pipeValue.data(), pipeValue.length());
				}
			}
			memset(buffer, 0, 1024);
		}
		if (pfds[1].revents == POLLIN) {
			readLength = read(pfds[1].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Error reading from message." << std::endl;
			} else {
				std::string pipeValue(buffer);
				std::cout << "Message output: " << pipeValue << std::endl;
				if (pipeValue == "exit") {
					std::cout << "Terminating socket thread." << std::endl;
					break;
				}
			}
			memset(buffer, 0, 1024);
		}
		if (pfds[2].revents == POLLIN) {
			//TODO: Handler to send message to server.
			memset(buffer, 0, 1024);
		}
		/*
		readLength = read(socket_fd, buffer, 1024);
		if (readLength < 0) {
			std::cerr << "Error reading from socket." << std::endl;
		} else if (readLength == 0) {
			std::cout << "Socket closed, ending handler." << std::endl;
		} else {
			std::string bufferString(buffer);
			std::cout << "Message from server: " << bufferString << std::endl;
		}*/
	}
}
