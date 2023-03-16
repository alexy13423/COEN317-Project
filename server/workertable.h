#ifndef WORKER_TABLE_H
#define WORKER_TABLE_H

#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <pqxx/pqxx>

enum class WorkerStatus { idle = 0, pending = 1, active = 2 };

class Worker {
	public:
		Worker(int, std::string);
		std::string getCurrentJobString();
		WorkerStatus getActivityStatus();
		void setCurrentJobString(std::string);
		int getWorkerId();
		//void setPending();
		//void setInactive();
		void setActivityStatus(WorkerStatus);
		std::string serializeWorker(pqxx::connection*);
	private:
		int worker_id;
		//bool is_active;
		WorkerStatus active_status;
		std::string ip_address;
		std::string current_job_string;
};

class WorkerTable {
	public:
		WorkerTable();
		bool checkWorkerAvailable();
		//int getAvailableWorker();
		Worker* getAvailableWorker();
		//void setWorkerStatus(int, bool);
		void setWorkerStatus(int, WorkerStatus);
		std::string getWorkerTableData(pqxx::connection*);
		void addNewWorker(int, std::string);

		std::mutex worker_table_mutex;
		std::condition_variable worker_table_cv;
	private:
		std::vector<Worker> workers;
};

#endif
