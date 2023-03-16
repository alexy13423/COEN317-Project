#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <mutex>
#include <chrono>
#include <pqxx/pqxx>

#include "workertable.h"

Worker::Worker(int worker_id, std::string ip_address) {
	this->worker_id = worker_id;
	this->ip_address = ip_address;

	//this->is_active = false;
	this->active_status = WorkerStatus::idle;
	this->current_job_string = "idle";
}

WorkerStatus Worker::getActivityStatus() {
	return active_status;
}

std::string Worker::getCurrentJobString() {
	return this->current_job_string;
}

void Worker::setCurrentJobString(std::string newJob) {
	this->current_job_string = newJob;
	//this->is_active = true;
	this->active_status = WorkerStatus::active;
}

int Worker::getWorkerId() {
	return worker_id;
}

void Worker::setActivityStatus(WorkerStatus newStatus) {
	active_status = newStatus;
}

std::string Worker::serializeWorker(pqxx::connection *dbConnection) {
	std::stringstream worker_serialization;
	pqxx::work txn(*dbConnection);

	pqxx::row firstJobQuery = txn.exec1("select sum(firstjob), count(firstjobid) from frame where firstjobid=" + pqxx::to_string(worker_id));
	pqxx::row secondJobQuery = txn.exec1("select sum(secondjob), count(secondjobid) from frame where secondjobid=" + pqxx::to_string(worker_id));

	float firstJobTotal = atof(firstJobQuery[0].c_str());
	int firstJobEntries = atoi(firstJobQuery[1].c_str());
	float secondJobTotal = atof(secondJobQuery[0].c_str());
	int secondJobEntries = atoi(secondJobQuery[1].c_str());

	float firstJobAverage = firstJobTotal / firstJobEntries;
	float secondJobAverage = secondJobTotal / secondJobEntries;

	worker_serialization << "Worker id: " << worker_id << " IP Address: " << ip_address << " Complete frame detections: " << firstJobEntries
						<< " Average frame detection time: " << firstJobAverage << " Complete frame drawings: " << secondJobEntries
						<< " Average frame drawing time: " << secondJobAverage << '\n';

	std::string result = worker_serialization.str();
	return result;
}

WorkerTable::WorkerTable() {
	//i guess there isn't really much to do here at all
}

void WorkerTable::addNewWorker(int new_worker_fd, std::string new_worker_ip) {
	Worker newWorker(new_worker_fd, new_worker_ip);
	workers.push_back(newWorker);
}

bool WorkerTable::checkWorkerAvailable() {
	for (int i = 0; i < workers.size(); i++) {
		if (workers.at(i).getActivityStatus() == WorkerStatus::idle) {
			return true;
		}
	}
	return false;
}

Worker* WorkerTable::getAvailableWorker() {
	for (int i = 0; i < workers.size(); i++) {
		if (workers.at(i).getActivityStatus() == WorkerStatus::idle) {
			return &(workers.at(i));
		}
	}
	return (Worker*) NULL;
}

void WorkerTable::setWorkerStatus(int workerId, WorkerStatus newStatus) {
	for (int i = 0; i < workers.size(); i++) {
		if (workers.at(i).getWorkerId() == workerId) {
			workers.at(i).setActivityStatus(newStatus);
		}
	}
}

std::string WorkerTable::getWorkerTableData(pqxx::connection *dbConnection) {
	std::stringstream worker_table_serialization;

	for (int i = 0; i < workers.size(); i++) {
		std::string worker_serialization = workers.at(i).serializeWorker(dbConnection);
		worker_table_serialization << worker_serialization;
		//std::cout << "Worker data: " << worker_serialization;
	}

	std::string result = worker_table_serialization.str();
	return result;
}
