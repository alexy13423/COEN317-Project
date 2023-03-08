#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <cstring>

#include <stdlib.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "socket_handler.h"
#include "processing_handler.h"

//void socketThread(std::vector<int>, int);
//void processingThread(std::vector<int>, int);

int main(int argc, char *argv[]) {
	int pipe_status;

	//Create the pipes for handling communication between socket and
	//processing threads.
	int socket_processing_fds[2];
	pipe_status = pipe(socket_processing_fds);
	if (pipe_status < 0) {
		std::cerr << "Pipe creation failed (2)." << std::endl;
		return -1;
	} else {
		std::cout << "Pipe creation success (2)." << std::endl;
	}

	int processing_socket_fds[2];
	pipe_status = pipe(processing_socket_fds);
	if (pipe_status < 0) {
		std::cerr << "Pipe creation failed (3)." << std::endl;
		return -1;
	} else {
		std::cout << "Pipe creation success (3)." << std::endl;
	}

	//Create the pipes used to send commands from shell to the threads.
	int main_processing_fds[2];
	pipe_status = pipe(main_processing_fds);
	if (pipe_status < 0) {
		std::cerr << "Pipe creation failed (4)." << std::endl;
		return -1;
	} else {
		std::cout << "Pipe creation success (4)." << std::endl;
	}

	int main_socket_fds[2];
	pipe_status = pipe(main_socket_fds);
	if (pipe_status < 0) {
		std::cerr << "Pipe creation failed (5)." << std::endl;
		return -1;
	} else {
		std::cout << "Pipe creation success (5)." << std::endl;
	}

	//Create the socket used for communication with the server.
	/*
	struct sockaddr_in serv_addr;
	int client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_fd < 0) {
		std::cout << "Socket creation error." << std::endl;
		return -1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(1111);

	if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
		std::cerr << "Error: Invalid address/address not supported." << std::endl;
		return -1;
	}

	int status = -1;
	if ((status = connect(client_fd, (struct sockaddr*) &serv_addr, sizeof(serv_addr))) < 0) {
		std::cerr << "Error: connection failed." << std::endl;
		return -1;
	}
	*/

	std::vector<int> socket_thread_fds;
	socket_thread_fds.push_back(main_socket_fds[0]);
	//socket_thread_fds.push_back(client_fd);
	socket_thread_fds.push_back(processing_socket_fds[0]);

	std::vector<int> processing_thread_fds;
	processing_thread_fds.push_back(main_processing_fds[0]);
	processing_thread_fds.push_back(socket_processing_fds[0]);

	//Startup socket thread.
	SocketHandler socket(socket_thread_fds, socket_processing_fds[1]);
	//std::thread sockThread(socketThread, socket_thread_fds, socket_processing_fds[1]);
	std::thread sockThread(&SocketHandler::socketThread, socket);

	//Startup processing thread.
	ProcessHandler process(processing_thread_fds, processing_socket_fds[1]);
	//std::thread processThread(processingThread, processing_thread_fds, processing_socket_fds[1]);
	std::thread processThread(&ProcessHandler::processThread, process);

	//Loop on input from user until "exit" is input.
	
	std::string input = "";
	std::cout << "Waiting on user input..." << std::endl;
	while(true) {
		std::cin >> input;
		if (input == "exit") {
			std::cout << "Exiting program." << std::endl;
			break;
		} else {
			std::cout << "User string: " << input << std::endl;

			//TEMP: pseudo packet write to socket thread for pseudo server message
			write(main_socket_fds[1], input.data(), input.length());
		}
	}

	//Cleanup
	std::string exit_message = "exit";
	write(main_socket_fds[1], exit_message.data(), exit_message.length());
	write(main_processing_fds[1], exit_message.data(), exit_message.length());
	sockThread.join();
	processThread.join();
	//close(client_fd);
	std::cout << "End of parent process." << std::endl;
	return 0;
}
