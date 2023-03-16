#ifndef CLIENT_SOCKET_H
#define CLIENT_SOCKET_H

#include <vector>
#include <pqxx/pqxx>

#include "clienttable.h"
#include "workertable.h"
#include "jobqueue.h"

class ClientSocketHandler {
	public:
		ClientSocketHandler(std::vector<int>, ClientTable*, WorkerTable*, JobQueue*, pqxx::connection*);
		void clientSocketThread();
	private:
		std::vector<int> client_thread_fds;
		ClientTable *clientTable;
		WorkerTable *workerTable;
		JobQueue *jobQueue;
		pqxx::connection *dbConnection;
};

#endif
