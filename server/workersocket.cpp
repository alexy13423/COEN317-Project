#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <iostream>

#include <unistd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "workersocket.h"

//socket_thread_fds[0] = from main (for commands)
//socket_thread_fds[1] = from processing (for outbound jobs)
//socket_thread_fds[2] = server-worker socket (for inbound connections)
//ALL SUBSEQUENT ENTRIES - for connected worker sockets
WorkerSocketHandler::WorkerSocketHandler(std::vector<int> socket_thread_fds, int worker_client_fd, WorkerTable *workerTable, JobQueue *jobQueue, pqxx::connection *dbConnection) {
	this->socket_thread_fds = socket_thread_fds;
	this->workerTable = workerTable;
	this->jobQueue = jobQueue;
	this->dbConnection = dbConnection;
	this->worker_client_fd = worker_client_fd;
}

void WorkerSocketHandler::workerSocketThread() {
	bool keepLooping = true;
	std::cout << "Worker socket handler started." << std::endl;
	char buffer[1024] = { 0 };
	int status = 0, readLength = 0;
	while (keepLooping) {
		int pfds_length = socket_thread_fds.size();
		struct pollfd pfds[pfds_length];
		for (int i = 0; i < pfds_length; i++) {
			pfds[i].fd = socket_thread_fds.at(i);
			pfds[i].events = POLLIN;
		}
		std::cout << "Worker socket thread polling..." << std::endl;
		status = poll(pfds, pfds_length, -1);
		if (status < 0) {
			std::cerr << "Error with poll (worker socket)." << std::endl;
			exit(1);
		}
		if (status == 0) {
			std::cerr << "Poll returned 0 events (worker socket)?" << std::endl;
			exit(1);
		}
		if (pfds[0].revents == POLLIN) {
			std::cout << "pfds[0]: main pipe input" << std::endl;
			readLength = read(pfds[0].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Worker socket handler: Error reading from main pipe." << std::endl;
				exit(1);
			} else {
				std::string pipeValue(buffer);
				std::cout << "Main -> worker socket message output: " << pipeValue << std::endl;
				if (pipeValue == "exit") {
					std::cout << "Terminating worker socket thread." << std::endl;
					keepLooping = false;
				} else {
					std::vector<std::string> tokens;
					std::stringstream check1(pipeValue);
					check1 << pipeValue;
					std::string intermediate;
					while (getline(check1, intermediate, ' ')) {
						tokens.push_back(intermediate);
					}

					//Handle processing of tokenized pipe string.
					if (tokens[0] == "sendworker") {
						int target_worker_fd = std::stoi(tokens[1]);
						bool target_found = false;
						for (int i = 3; i < pfds_length; i++) {
							if (socket_thread_fds.at(i) == target_worker_fd) {
								target_found = true;
							}
						}
						if (target_found) {
							std::stringstream worker_message_stream;
							for (int i = 2; i < tokens.size() - 1; i++) {
								worker_message_stream << tokens.at(i) << " ";
							}
							worker_message_stream << tokens.at(tokens.size() - 1);
							std::string worker_message = worker_message_stream.str();
							std::cout << "Worker socket handler: Sending to worker id " << target_worker_fd << ": " << worker_message << std::endl;
							char *message_buffer = worker_message.data();
							send(target_worker_fd, message_buffer, strlen(message_buffer), 0);
						} else {
							std::cerr << "Worker socket handler: Unable to write message to workers (worker not found)." << std::endl;
						}
					}
				}
			}
			memset(buffer, 0, 1024);
		}
		if (pfds[1].revents == POLLIN) {
			std::cout << "pfds[1]: processing pipe input" << std::endl;
			readLength = read(pfds[1].fd, buffer, 1024);
			//allow processing to send another message now
			std::unique_lock socket_lock(jobQueue->socket_guard_mutex);
			jobQueue->socket_guard = 1;
			socket_lock.unlock();
			if (readLength < 0) {
				std::cerr << "Worker socket handler: Error reading message from processing pipe." << std::endl;
				exit(1);
			} else {
				std::string pipeValue(buffer);
				std::cout << "Processing -> worker socket message output: " << pipeValue << std::endl;
				std::vector<std::string> tokens;
				std::stringstream check1(pipeValue);
				check1 << pipeValue;
				std::string intermediate;
				while (getline(check1, intermediate, ' ')) {
					tokens.push_back(intermediate);
				}
				int target_client_id = std::stoi(tokens[1]);
				bool target_found = false;
				for (int i = 3; i < pfds_length; i++) {
					if (socket_thread_fds.at(i) == target_client_id) {
						target_found = true;
					}
				}
				if (target_found) {	
					std::stringstream worker_message_stream;
					for (int i = 2; i < tokens.size() - 1; i++) {
						worker_message_stream << tokens.at(i) << " ";
					}
					worker_message_stream << tokens.at(tokens.size() - 1);
					std::string worker_message = worker_message_stream.str();
					std::cout << "Worker socket handler: Sending to worker id " << target_client_id << ": " << worker_message << std::endl;
					send(target_client_id, worker_message.data(), worker_message.length(), 0);
				} else {
					std::cerr << "Worker socket handler: Unable to write message from processing job to workers (worker not found)." << std::endl;
					exit(1);
				}
			}
			memset(buffer, 0, 1024);
		}
		if (pfds[2].revents == POLLIN) {
			std::cout << "pfds[2]: new connection" << std::endl;
			//Server has an inbound connection, call accept() and add an entry to the worker_socket_fds.
			int new_socket;

			struct sockaddr_in worker_address;
			int worker_addrlen = sizeof(worker_address);

			if ((new_socket = accept(pfds[2].fd, (struct sockaddr*)&worker_address, (socklen_t*) &worker_addrlen)) < 0) {
				std::cerr << "Worker socket handler: Error on accepting connection." << std::endl;
				exit(1);
			} else {
				std::cout << "Worker socket handler: Accepting new connection, fd: " << new_socket << std::endl;
				socket_thread_fds.push_back(new_socket);

				struct sockaddr_in* worker_ipv4_address = (struct sockaddr_in*) &worker_address;
				struct in_addr worker_ip = worker_ipv4_address->sin_addr;

				char ip_buffer[INET_ADDRSTRLEN];
				inet_ntop( AF_INET, &worker_ip, ip_buffer, INET_ADDRSTRLEN );

				std::string ipString(ip_buffer);
				std::cout << "Worker ip address detected: " << ipString << std::endl;
				{
					std::lock_guard<std::mutex> guard(workerTable->worker_table_mutex);
					workerTable->addNewWorker(new_socket, ipString);
				}
				workerTable->worker_table_cv.notify_one();

				std::stringstream client_id_stream;
				client_id_stream << "clientid " << new_socket;
				std::string client_id_message = client_id_stream.str();
				send(new_socket, client_id_message.data(), client_id_message.length(), 0);
			}
		}
		std::vector<int> sockets_to_remove;
		for (int i = 3; i < socket_thread_fds.size(); i++) {
			if (pfds[i].revents == POLLIN) {
				int worker_id = pfds[i].fd;
				readLength = read(pfds[i].fd, buffer, 1024);
				if (readLength < 0) {
					std::cerr << "Worker socket handler: Error reading from client fd: " << worker_id << std::endl;
					exit(1);
				} else {
					std::string pipeValue(buffer);
					std::cout << "Worker socket handler: Message from client fd " << worker_id << " is: " << pipeValue << std::endl;

					if (pipeValue.length() == 0) {
						std::cout << "Worker socket handler: Client fd " << worker_id << " seems to be closed, closing server side" << std::endl;
						//int fd_to_remove = pfds[i].fd;
						sockets_to_remove.push_back(i);
					} else {
						//Determine the type of message the worker sent.
						std::vector<std::string> tokens;
						std::stringstream check1(pipeValue);
						check1 << pipeValue;
						std::string intermediate;
						while (getline(check1, intermediate, ' ')) {
							tokens.push_back(intermediate);
						}
						JobType inbound_job_type;
						int jobId, frameId = -1, totalFrameCount = -1;
						if (tokens[1] == "preprocessing") {
							inbound_job_type = JobType::preprocessing;
							jobId = std::stoi(tokens[2]);
							if (tokens[0] == "jobcomplete")
								totalFrameCount = std::stoi(tokens[4]);
						} else if (tokens[1] == "coordinate") {
							inbound_job_type = JobType::coordinate;
							jobId = std::stoi(tokens[2]);
							frameId = std::stoi(tokens[3]);
						} else if (tokens[1] == "drawing") {
							inbound_job_type = JobType::drawing;
							jobId = std::stoi(tokens[2]);
							frameId = std::stoi(tokens[3]);
						} else if (tokens[1] == "postprocessing") {
							inbound_job_type = JobType::postprocessing;
							jobId = std::stoi(tokens[2]);
						} else {
							std::cerr << "Error in processing response from a worker." << std::endl;
							exit(1);
						}
						int inbound_client_id = std::stoi(tokens[4]);

						if (tokens[0] == "jobaccept") {
							//TODO: Set this worker to active, with the job it is working on.
							//TODO: Set the accepted job to active.
							std::unique_lock worker_table_lock(workerTable->worker_table_mutex);
							workerTable->setWorkerStatus(inbound_client_id, WorkerStatus::active);
							worker_table_lock.unlock();

							std::unique_lock job_queue_lock(jobQueue->job_queue_mutex);
							Job *targetJob = jobQueue->findJobFromInfo(jobId, frameId, inbound_job_type);
							targetJob->assignJob(inbound_client_id);
							job_queue_lock.unlock();
						}

						if (tokens[0] == "jobcomplete") {
							std::unique_lock worker_table_lock(workerTable->worker_table_mutex);
							workerTable->setWorkerStatus(inbound_client_id, WorkerStatus::idle);
							worker_table_lock.unlock();
							workerTable->worker_table_cv.notify_one();

							std::unique_lock job_queue_lock(jobQueue->job_queue_mutex);
							Job *targetJob = jobQueue->findJobFromInfo(jobId, frameId, inbound_job_type);
							targetJob->completeJob();

							if (tokens[1] == "preprocessing") {
								pqxx::work txn(*dbConnection);
								pqxx::row videoQueryRow = txn.exec1("select framecount from video where videoid = " + pqxx::to_string(jobId));
								int frameCount = atoi(videoQueryRow[0].c_str());
								for (int i = 1; i <= frameCount; i++) {
									jobQueue->createJob(jobId, i, JobType::coordinate);
								}
							} else if (tokens[1] == "coordinate") {
								jobQueue->createJob(jobId, frameId, JobType::drawing);
							} else if (tokens[1] == "drawing") {
								pqxx::work txn(*dbConnection);
								txn.exec0("update frame set processed = 1 where videoid = " + pqxx::to_string(jobId) + " and frameid = " + pqxx::to_string(frameId));
								pqxx::row processedCountRow = txn.exec1("select count(*) from frame where videoid = " + pqxx::to_string(jobId) + " and processed = 0");
								txn.commit();
								int remainingCount = atoi(processedCountRow[0].c_str());
								std::cout << "Remaining frames to process for job " << jobId << ": " << remainingCount << std::endl;
								if (remainingCount == 0) {
									jobQueue->createJob(jobId);
								}
							} else if (tokens[1] == "postprocessing") {
								pqxx::work txn(*dbConnection);
								pqxx::row clientIdRow = txn.exec1("select clientid from video where videoid = " + pqxx::to_string(jobId));
								int client_id = atoi(clientIdRow[0].c_str());
								std::stringstream final_output;
								final_output << "jobcomplete " << client_id << " " << jobId;
								std::string final_string = final_output.str();
								write(worker_client_fd, final_string.data(), final_string.length());
							}
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
				socket_thread_fds.erase(socket_thread_fds.begin() + fd_to_remove);
			}
		}
	}

	//Do cleanup of all our sockets before we go.
	for (int i = 0; i < socket_thread_fds.size(); i++) {
		close(socket_thread_fds.at(i));
	}
}
