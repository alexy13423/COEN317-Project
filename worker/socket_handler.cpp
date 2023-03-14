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
		if (pfds[0].revents == POLLIN) { //main thread
			readLength = read(pfds[0].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Error reading from main pipe." << std::endl;
			} else {
				std::string pipeValue(buffer);
				std::cout << "Message output: " << pipeValue << std::endl;
				if (pipeValue == "exit") {
					std::cout << "Terminating socket thread." << std::endl;
					break;
				} /*else { //TEMPORARY PSEUDOMESSAGE HANDLER
					write(socket_processing_fd, pipeValue.data(), pipeValue.length());
				} */
			}
			memset(buffer, 0, 1024);
		}
		if (pfds[1].revents == POLLIN) { //server message thread
			readLength = read(pfds[1].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Error reading from message." << std::endl;
			} else if (readLength == 0) {
				std::cout << "Server seems to have shut down, shutting down client" << std::endl;
				exit(0);
			} else {
				std::string pipeValue(buffer);
				//std::cout << "Message output from server: " << pipeValue << std::endl;
				write(socket_processing_fd, pipeValue.data(), pipeValue.length());
			}
			memset(buffer, 0, 1024);
		}
		if (pfds[2].revents == POLLIN) {
			//TODO: Handler to send message to server.
			readLength = read(pfds[2].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Error reading from message." << std::endl;
			} else {
				std::string pipeValue(buffer);
				std::cout << "Sending message to server: " << pipeValue << std::endl;

				char *message_to_server = pipeValue.data();
				int server_fd = socket_thread_fds.at(1);
				send(server_fd, message_to_server, strlen(message_to_server), 0);
			}
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
