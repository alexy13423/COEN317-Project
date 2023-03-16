#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <thread>

#include <stdlib.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "FTPClient.h"

#define SERVER_ADDRESS "127.0.0.1"
#define SERVER_PORT 1112

class SocketHandler {
	public:
		SocketHandler(int, int);
		void socketThread();
	private:
		int socket_fd;
		int main_handler_fd;
};

int main(int argc, char *argv[]) {
	//Socket to server.
	int status = -1;
	struct sockaddr_in serv_addr;
	int client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_fd < 0) {
		std::cerr << "Error creating socket to server." << std::endl;
		return -1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(SERVER_PORT);

	if (inet_pton(AF_INET, SERVER_ADDRESS, &serv_addr.sin_addr) <= 0) {
		std::cerr << "Error: Invalid address/address not supported." << std::endl;
		return -1;
	}

	if ((status = connect(client_fd, (struct sockaddr*) &serv_addr, sizeof(serv_addr))) < 0) {
		std::cerr << "Error: connection failed." << std::endl;
		return -1;
	}

	//Main to socket thread.
	int main_socket_fds[2];
	status = pipe(main_socket_fds);
	if (status < 0) {
		std::cerr << "Error: failed to create pipe for main/socket communication." << std::endl;
		return -1;
	}

	//Socket handler.
	SocketHandler handler(client_fd, main_socket_fds[0]);
	std::thread socketThread(&SocketHandler::socketThread, handler);

	std::cout << "Welcome to the Puppy Swarm video pipeline!" << std::endl;
	std::cout << "Please enter the name of the file you want to process." << std::endl;

	std::string input;
	getline( std::cin, input );

	std::stringstream fileInputStream;
	fileInputStream << "input " << input;
	std::string fileInput = fileInputStream.str();
	write(main_socket_fds[1], fileInput.data(), fileInput.length());

	std::cout << "Waiting on further user input for commands (status/exit)..." << std::endl;
	while (true) {
		getline( std::cin, input );
		if (input == "exit") {
			std::cout << "Exiting program." << std::endl;
			break;
		} else if (input == "status") {
			std::cout << "Getting status from server..." << std::endl;
			write(main_socket_fds[1], input.data(), input.length());
		}
	}

	std::string exit_message = "exit";
	write(main_socket_fds[1], exit_message.data(), exit_message.length());
	
	std::cout << "End of program." << std::endl;
	return 0;
}

SocketHandler::SocketHandler(int socket_fd, int main_handler_fd) {
	this->socket_fd = socket_fd;
	this->main_handler_fd = main_handler_fd;
}

void SocketHandler::socketThread() {
	struct pollfd pfds[2];
	int status = 0;
	char buffer[1000000] = { 0 };
	int readLength = 0;

	pfds[0].fd = main_handler_fd;
	pfds[0].events = POLLIN;
	pfds[1].fd = socket_fd;
	pfds[1].events = POLLIN;

	embeddedmz::CFTPClient FTPClient([](const std::string& strLogMsg){ std::cout << strLogMsg << std::endl; });
	FTPClient.InitSession(SERVER_ADDRESS, 21, "PuppyApp", "Aailf13423");

	while (true) {
		status = poll(pfds, 2, -1);
		if (status < 0) {
			std::cerr << "Socket handler: Error with poll." << std::endl;
			exit(1);
		}
		if (status == 0) {
			std::cerr << "Socket handler: Error with poll (0 events)." << std::endl;
			exit(1);
		}
		if (pfds[0].revents == POLLIN) {
			readLength = read(pfds[0].fd, buffer, 1000000);
			if (readLength < 0) {
				std::cerr << "Socket handler: Error reading from main pipe." << std::endl;
				exit(1);
			} else {
				std::string pipeValue(buffer);
				if (pipeValue == "exit") {
					break;
				}

				std::vector<std::string> tokens;
				std::stringstream check1(pipeValue);
				std::string intermediate;
				while (getline(check1, intermediate, ' ')) {
					tokens.push_back(intermediate);
				}
				std::cout << "token[0]: " << tokens[0] << std::endl;

				if (tokens[0] == "input") {
					std::string fileName = tokens[1];
					std::stringstream destinationStream;
					destinationStream << "/inbox/" << fileName;
					std::string destinationPath = destinationStream.str();

					FTPClient.UploadFile(fileName, destinationPath, true);
					send(socket_fd, pipeValue.data(), pipeValue.length(), 0);
				}

				if (tokens[0] == "status") {
					send(socket_fd, pipeValue.data(), pipeValue.length(), 0);
				}
			}
			memset(buffer, 0, 1000000);
		}
		if (pfds[1].revents == POLLIN) {
			readLength = read(pfds[1].fd, buffer, 1000000);
			if (readLength < 0) {
				std::cerr << "Socket handler: Error reading from socket." << std::endl;
				exit(1);
			} else if (readLength == 0) {
				std::cout << "Socket handler: Socket closing detected, server seems to have shut down." << std::endl;
				exit(0);
			} else {
				std::string pipeValue(buffer);
				std::vector<std::string> tokens;
				std::stringstream check1(pipeValue);
				std::string intermediate;
				while (getline(check1, intermediate, ' ')) {
					tokens.push_back(intermediate);
				}

				std::cout << "input token[0]: " << tokens[0] << std::endl;

				if (tokens[0] == "output") {
					int jobId = std::stoi(tokens[1]);
					std::cout << "Job completion detected, job id: " << jobId << std::endl;
					std::stringstream sourceStream;
					sourceStream << "/" << jobId << "/output/output.mp4";
					std::string sourcePath = sourceStream.str();
					std::string destinationPath = "./output.mp4";
					FTPClient.DownloadFile(destinationPath, sourcePath);

					std::cout << "Final video has been downloaded to output.mp4." << std::endl;
					exit(0);
				}

				if (tokens[0] == "status") {
					std::cout << "Status update:" << std::endl;
					std::string statusUpdate = pipeValue.substr(7);
					std::cout << statusUpdate << std::endl;
				}
			}
			memset(buffer, 0, 1000000);
		}
	}
}
