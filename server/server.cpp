#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <pqxx/pqxx>

#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "workertable.h"
#include "jobqueue.h"
#include "clienttable.h"
#include "workersocket.h"
#include "clientsocket.h"
#include "processing.h"

#define WORKER_PORT 1111
#define CLIENT_PORT 1112

void clientSocketThread();
void workerSocketThread();

int main(int argc, char *argv[]) {
	//Create fds for handling worker communication.
	int server_worker_socket_fd, new_socket_status;
	struct sockaddr_in worker_address;
	int opt = 1;
	int worker_addrlen = sizeof(worker_address);

	//Create fds for handling client communication.
	int server_client_socket_fd, new_client_status;
	struct sockaddr_in client_address;
	int client_addrlen = sizeof(client_address);

	//Create pipe for sending messages to workers.
	int main_worker_fds[2];
	int pipe_status = pipe(main_worker_fds);
	if (pipe_status < 0) {
		std::cerr << "Worker pipe creation failed." << std::endl;
		return -1;
	} else {
		std::cout << "Worker pipe creation success." << std::endl;
	}

	//Create pipe for sending messages to clients. (except not really, this is just for shutdown)
	int main_client_fds[2];
	pipe_status = pipe(main_client_fds);
	if (pipe_status < 0) {
		std::cerr << "Client pipe creation failed." << std::endl;
		return -1;
	} else {
		std::cout << "Client pipe creation success." << std::endl;
	}

	//Create pipe for handling outbound jobs.
	int processing_worker_fds[2];
	pipe_status = pipe(processing_worker_fds);
	if (pipe_status < 0) {
		std::cerr << "Processing to worker socket pipe creation failed." << std::endl;
		return -1;
	} else {
		std::cout << "Processing to worker socket pipe creation success." << std::endl;
	}

	//Create pipe for handling job completion messages.
	int worker_client_fds[2];
	pipe_status = pipe(worker_client_fds);
	if (pipe_status < 0) {
		std::cerr << "Worker to client socket pipe creation failed." << std::endl;
		return -1;
	} else {
		std::cout << "Worker to client socket pipe creation success." << std::endl;
	}

	//Create the socket that handles incoming connections from workers..
	server_worker_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_worker_socket_fd < 0) {
		std::cerr << "Worker socket creation error." << std::endl;
		exit(1);
	}

	//Attach the worker socket to the worker port.
	if (setsockopt(server_worker_socket_fd, SOL_SOCKET,
			SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
		std::cerr << "Error when calling setsockopt." << std::endl;
		exit(1);
	}
	
	worker_address.sin_family = AF_INET;
	worker_address.sin_addr.s_addr = INADDR_ANY;
	worker_address.sin_port = htons(WORKER_PORT);

	if (bind(server_worker_socket_fd, (struct sockaddr*)&worker_address,
			sizeof(worker_address)) < 0) {
		std::cerr << "Worker socket bind error." << std::endl;
		exit(1);
	}

	if (listen(server_worker_socket_fd, 3) < 0) {
		std::cerr << "Worker socket listen error." << std::endl;
		exit(1);
	}

	//Create the socket that handles incoming connections from clients.
	server_client_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_client_socket_fd < 0) {
		std::cerr << "Client socket creation error." << std::endl;
		exit(1);
	}

	//Attach the client socket to the client port.
	if (setsockopt(server_client_socket_fd, SOL_SOCKET,
			SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
		std::cerr << "Error when calling setsockopt. (client)" << std::endl;
		exit(1);
	}

	client_address.sin_family = AF_INET;
	client_address.sin_addr.s_addr = INADDR_ANY;
	client_address.sin_port = htons(CLIENT_PORT);

	if (bind(server_client_socket_fd, (struct sockaddr*)&client_address,
			sizeof(client_address)) < 0) {
		std::cerr << "Client socket bind error." << std::endl;
		exit(1);
	}

	if (listen(server_client_socket_fd, 3) < 0) {
		std::cerr << "Client socket listen error." << std::endl;
		exit(1);
	}

	//Create the data structures we need for the server.
	WorkerTable workerTable;
	JobQueue jobQueue;
	ClientTable clientTable;

	std::string connectionString = "postgresql://postgres:Aailf13423@localhost/puppyswarm";
	pqxx::connection dbConnection{connectionString};

	//TODO: Create fds for handling client communication.
	std::vector<int> worker_thread_fds;
	worker_thread_fds.push_back(main_worker_fds[0]);
	worker_thread_fds.push_back(processing_worker_fds[0]);
	worker_thread_fds.push_back(server_worker_socket_fd);

	std::vector<int> client_thread_fds;
	client_thread_fds.push_back(main_client_fds[0]);
	client_thread_fds.push_back(worker_client_fds[0]);
	client_thread_fds.push_back(server_client_socket_fd);

	WorkerSocketHandler workerHandler(worker_thread_fds, worker_client_fds[1], &workerTable, &jobQueue, &dbConnection);
	std::thread workerThread(&WorkerSocketHandler::workerSocketThread, workerHandler);

	ProcessingHandler processingHandler(&workerTable, &jobQueue, processing_worker_fds[1], &dbConnection);
	std::thread processingThread(&ProcessingHandler::processingThread, processingHandler);

	ClientSocketHandler clientHandler(client_thread_fds, &clientTable, &workerTable, &jobQueue, &dbConnection);
	std::thread clientThread(&ClientSocketHandler::clientSocketThread, clientHandler);

	std::string input = "";
	std::cout << "Waiting on user input..." << std::endl;
	while(true) {
		//std::cin >> input;
		getline( std::cin, input );
		if (input == "exit") {
			std::cout << "Exiting program." << std::endl;
			break;
		} else {
			std::cout << "User string: " << input << std::endl;

			//TEMP: pseudo packet write to socket thread for pseudo server message
			//write(main_worker_fds[1], input.data(), input.length());

			//TEMP: Create a job based on the input. Then signal that a pending job is available.
			if (input == "testjob") {
				std::string testFilename("IMG-2911.MOV");
				{
					const std::lock_guard<std::mutex> lock(jobQueue.job_queue_mutex);
					jobQueue.createJob(1, testFilename);
				}
				
				jobQueue.job_queue_cv.notify_one();
			}
		}
	}

	std::string exit_message = "exit";
	write(main_worker_fds[1], exit_message.data(), exit_message.length());
	//write(main_processing_fds[1], exit_message.data(), exit_message.length());
	//write(main_client_fds[1], exit_message.data(), exit_message.length());

	processingHandler.doShutdown();
	workerTable.worker_table_cv.notify_one();
	jobQueue.job_queue_cv.notify_one();

	workerThread.join();
	processingThread.join();

	close(server_worker_socket_fd);
	close(main_worker_fds[1]);
	close(processing_worker_fds[1]);
	
	std::cout << "End of parent process." << std::endl;
	return 0;
}

void workerSocketThread(int socket_fd) {
}
