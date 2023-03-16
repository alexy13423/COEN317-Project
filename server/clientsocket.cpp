#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <pqxx/pqxx>

#include <unistd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "clientsocket.h"

ClientSocketHandler::ClientSocketHandler(std::vector<int> client_thread_fds, ClientTable *clientTable, WorkerTable *workerTable, JobQueue *jobQueue, pqxx::connection *dbConnection) {
	this->client_thread_fds = client_thread_fds;
	this->clientTable = clientTable;
	this->workerTable = workerTable;
	this->jobQueue = jobQueue;
	this->dbConnection = dbConnection;
}

void ClientSocketHandler::clientSocketThread() {
	bool keepLooping = true;
	int status = 0, readLength = 0;
	char buffer[1024] = { 0 };
	while (keepLooping) {
		int pfds_length = client_thread_fds.size();
		struct pollfd pfds[pfds_length];
		for (int i = 0; i < pfds_length; i++) {
			pfds[i].fd = client_thread_fds.at(i);
			pfds[i].events = POLLIN;
		}
		std::cout << "Client socket thread polling..." << std::endl;
		status = poll(pfds, pfds_length, -1);
		if (status < 0) {
			std::cerr << "Error with poll (client socket)." << std::endl;
			exit(1);
		}
		if (status == 0) {
			std::cerr << "Poll returned 0 events (client socket)?" << std::endl;
			exit(1);
		}

		if (pfds[0].revents == POLLIN) {//main -> client socket thread
			readLength = read(pfds[0].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Client socket handler: Error reading from main pipe." << std::endl;
				exit(1);
			} else {
				std::string pipeValue(buffer);
				if (pipeValue == "exit") {
					std::cout << "Shutting down client socket." << std::endl;
					keepLooping = false;
				}
			}
			memset(buffer, 0, 1024);
		}
		if (pfds[1].revents == POLLIN) {//worker socket -> client socket thread
			readLength = read(pfds[1].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Client socket handler: Error reading from worker socket pipe." << std::endl;
				exit(1);
			} else {
				std::string pipeValue(buffer);
				std::vector<std::string> tokens;
				std::stringstream check1(pipeValue);
				//check1 << pipeValue;
				std::string intermediate;
				while (getline(check1, intermediate, ' ')) {
					tokens.push_back(intermediate);
				}
				std::string check = tokens[0];
				if (check == "jobcomplete") {
					int associated_client_id = std::stoi(tokens[1]);
					int completed_job_id = std::stoi(tokens[2]);

					std::stringstream downloadStream;
					downloadStream << "output " << completed_job_id;
					std::string downloadPath = downloadStream.str();
					send(associated_client_id, downloadPath.data(), downloadPath.length(), 0);
				}
			}
			memset(buffer, 0, 1024);
		}
		if (pfds[2].revents == POLLIN) {//client server socket
			int new_socket;

			struct sockaddr_in client_address;
			int client_addrlen = sizeof(client_address);

			if ((new_socket = accept(pfds[2].fd, (struct sockaddr*)&client_address, (socklen_t*) &client_addrlen)) < 0) {
				std::cerr << "Client socket handler: Error on accepting connection." << std::endl;
				exit(1);
			} else {
				std::cout << "Client socket handler: Accepting new connection, fd: " << new_socket << std::endl;
				client_thread_fds.push_back(new_socket);

				struct sockaddr_in* client_ipv4_address = (struct sockaddr_in*) &client_address;
				struct in_addr client_ip = client_ipv4_address->sin_addr;

				char ip_buffer[INET_ADDRSTRLEN];
				inet_ntop( AF_INET, &client_ip, ip_buffer, INET_ADDRSTRLEN );

				std::string ipString(ip_buffer);
				std::cout << "Client ip address detected: " << ipString << std::endl;
				clientTable->addNewClient(new_socket);
			}
		}
		std::vector<int> sockets_to_remove;
		for (int i = 3; i < pfds_length; i++) {//all connected client connection fds
			if (pfds[i].revents == POLLIN) {
				int client_id = pfds[i].fd;
				readLength = read(pfds[i].fd, buffer, 1024);
				if (readLength < 0) {
					std::cerr << "Client socket handler: Error reading from client fd: " << client_id << std::endl;
					exit(1);
				} else {
					std::string pipeValue(buffer);
					std::cout << "Client socket handler: Message from client fd " << client_id << " is: " << pipeValue << std::endl;

					if (pipeValue.length() == 0) {
						std::cout << "Client socket handler: Client fd " << client_id << " seems to be closed, closing server side" << std::endl;
						sockets_to_remove.push_back(i);
					} else if (pipeValue == "status") {
						//TODO: Status handler here.
						std::cout << "Getting status." << std::endl;
						std::stringstream status_stream;

						std::unique_lock job_queue_lock(jobQueue->job_queue_mutex);
						std::string queue_serialization = jobQueue->getJobQueueData();
						job_queue_lock.unlock();

						std::unique_lock worker_table_lock(workerTable->worker_table_mutex);
						std::string table_serialization = workerTable->getWorkerTableData(dbConnection);
						worker_table_lock.unlock();

						status_stream << "status " << std::endl << queue_serialization << std::endl << table_serialization << std::endl;
						std::string status_string = status_stream.str();
						send(client_id, status_string.data(), status_string.length(), 0);
					} else {
						std::vector<std::string> tokens;
						std::stringstream check1(pipeValue);
						check1 << pipeValue;
						std::string intermediate;
						while (getline(check1, intermediate, ' ')) {
							tokens.push_back(intermediate);
						}
						if (tokens[0] == "input") {
							std::string fileName = tokens[1];
							pqxx::work txn(*dbConnection);

							pqxx::row newIdRow = txn.exec1("insert into video (videoid, clientid) values (nextval('videoidentifiers'), " + pqxx::to_string(client_id) + ") returning videoid");
							txn.commit();
							int newJobId = atoi(newIdRow[0].c_str());
							
							std::unique_lock job_queue_lock(jobQueue->job_queue_mutex);
							jobQueue->createJob(newJobId, fileName);
							job_queue_lock.unlock();
							jobQueue->job_queue_cv.notify_one();
						}
					}
				}
				memset(buffer, 0, 1024);
			}
		}
		if (sockets_to_remove.size() > 0) {
			for (int i = sockets_to_remove.size() - 1; i >= 0; i--) {
				int fd_to_remove = sockets_to_remove.at(i);
				client_thread_fds.erase(client_thread_fds.begin() + fd_to_remove);
			}
		}
	}
}
