#ifndef WORKER_SOCKET_H
#define WORKER_SOCKET_H

#include <vector>
#include <pqxx/pqxx>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>

#include "workertable.h"
#include "jobqueue.h"

class WorkerSocketHandler {
    public:
        WorkerSocketHandler(std::vector<int>, int, WorkerTable*, JobQueue*, pqxx::connection*);
        void workerSocketThread();
    private:
        std::vector<int> socket_thread_fds;
		//int main_worker_fd;
        //int worksock_processing_fd;
		WorkerTable *workerTable;
		JobQueue *jobQueue;
		pqxx::connection *dbConnection;
		int worker_client_fd;
		//struct sockaddr_in *address;
		//int *addrlen;
};

#endif
