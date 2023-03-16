#ifndef PROCESSING_H
#define PROCESSING_H

#include <pqxx/pqxx>

#include "workertable.h"
#include "jobqueue.h"

class ProcessingHandler {
	public:
		ProcessingHandler(WorkerTable*, JobQueue*, int, pqxx::connection*);
		void doShutdown();
		void processingThread();
	private:
		bool keepLooping;

		WorkerTable *workerTable;
		JobQueue *jobQueue;
		//ClientTable *clientTable;
		int processing_worker_fd;
		//int processing_client_fd;

		pqxx::connection *dbConnection;
};

#endif
