#include "WorkqueueScheduler.h"
#include <mesos/resources.hpp>
#include <curl/curl.h>
#include <unistd.h>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <istream>
#include <string>
#include <vector>
#include <regex>
#include <chrono>

using namespace mesos;
std::string catalogString;
int constexpr length(const char* str)
{
    return *str ? 1 + length(str + 1) : 0;
}

constexpr size_t HEADER_LENGHT = length("tasks_waiting ");
size_t remainingOffset = 0;
size_t remaining = 0;
char readBuffer[CURL_MAX_WRITE_SIZE*2+1];
char *cursor;

// We simply put everything in a stringstream and read it afterwards.
size_t readToStream(void *p, size_t size, size_t nmemb, void *userdata) {
  std::stringstream *s = (std::stringstream *) userdata;
  *s << (char *) p;
  return size*nmemb;
}

WorkqueueScheduler::WorkqueueScheduler(const std::string &catalog,
                                       const std::string &docker,
                                       const std::vector<WorkqueueVolumeInfo> &volumes,
                                       const ExecutorInfo &executor)
: catalog_(catalog),
  docker_(docker),
  volumes_(volumes),
  workerInfo_(executor),
  workqueueMasterIdx_(0) 
{
}

void 
WorkqueueScheduler::registered(mesos::SchedulerDriver* driver,
                               const mesos::FrameworkID& frameworkId,
                               const mesos::MasterInfo& masterInfo) {
  std::cout << "Workqueue Mesos Scheduler registered" << std::endl;
}

void 
WorkqueueScheduler::reregistered(SchedulerDriver*, const MasterInfo& masterInfo) {
}

void
WorkqueueScheduler::disconnected(SchedulerDriver* driver) {
}

const float CPUS_PER_TASK = 1.0;
const int32_t MEM_PER_TASK = 1024;

// This is the method which does the actual heavy lifting:
//
// 1) Whenever a new offer arrives we contact the catalog to see which
//   masters are running.
// 2) Split the offer in 1 CPU chunks.
// 3) Create a workqueue worker for each chunk.
void
WorkqueueScheduler::resourceOffers(SchedulerDriver* driver,
                                   const std::vector<Offer>& offers)
{
  auto now = std::chrono::system_clock::now();

  // We check the workqueue scheduler only if there is no tasks running
  // reported by the previous check or once every 60 seconds to give it
  // enough time to update the tasks_waiting information.
  // This should allow us to guess the actual state of the cluster even
  // if the scheduler is updated only once every 60 seconds and yet to have 
  if (tasksRunning_ == 0
      || (now - lastUpdate_) > std::chrono::seconds(60)) {
    // 1) Update the masters list
    CURL *curl;
    curl = curl_easy_init();

    if (!curl) {
      std::cerr << "Unable to perform curl." << std::endl;
      return;
    }
    std::stringstream buffer;
    CURLcode res;
    std::cerr << "http://" + catalog_ + "/query.text" << std::endl;
    curl_easy_setopt(curl, CURLOPT_URL, ("http://" + catalog_ + "/query.text").c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, readToStream);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
      std::cerr << "Server not responding." << std::endl;
      return;
    }
    std::string key;
    std::string value;
    buffer.seekg(std::ios_base::beg);

    std::string line;
    while (std::getline(buffer, line, '\n')) {
      std::stringstream ss(line);
      std::string token;
      while (std::getline(ss, token, ' ')) {
        if (token == "tasks_waiting")
        {
          std::getline(ss, token, ' ');
          tasksWaiting_ = atoi(token.c_str());
        }
        if (token == "task_running")
        {
          std::getline(ss, token, ' ');
          tasksRunning_ = atoi(token.c_str());
        }
      }
      lastUpdate_ = std::chrono::system_clock::now();
    }

    std::cerr << tasksWaiting_ << std::endl;

  }

  for (size_t i = 0; i < offers.size(); i++) {
    const Offer& offer = offers[i];
    Resources remaining = offer.resources();

    static Resources TASK_RESOURCES = Resources::parse(
        "cpus:" + stringify<float>(CPUS_PER_TASK) +
        ";mem:" + stringify<size_t>(MEM_PER_TASK)).get();

    size_t maxTasks = 0;
    while (remaining.flatten().contains(TASK_RESOURCES)) {
      maxTasks++;
      remaining -= TASK_RESOURCES;
    }

    ContainerInfo container;
    container.set_type(ContainerInfo::DOCKER);

    ContainerInfo::DockerInfo dockerInfo;
    dockerInfo.set_image(docker_);
    container.mutable_docker()->CopyFrom(dockerInfo);

    for (auto &&v : volumes_)
    {
      Volume *volume = container.add_volumes();
      volume->set_host_path(v.host);
      volume->set_container_path(v.container);
      volume->set_mode(v.readOnly ? Volume::RO : Volume::RW);
    }

    CommandInfo command;
    command.set_value("work_queue_worker --workdir /mnt/mesos/sandbox --single-shot --debug -t 20 -C " + catalog_ +  " -N '.*'");
    // Launch as many workers as there are pending tasks.
    std::vector<TaskInfo> tasks;
    size_t toBeScheduled = std::min(tasksWaiting_, maxTasks);
    for (size_t i = 0; i < toBeScheduled ; i++) {
      TaskInfo task;
      task.set_name("Workqueue worker " + stringify<size_t>(workqueueMasterIdx_));
      task.mutable_task_id()->set_value(stringify<size_t>(workqueueMasterIdx_));
      task.mutable_slave_id()->MergeFrom(offer.slave_id());
      task.mutable_command()->MergeFrom(command);
      task.mutable_container()->MergeFrom(container);
      task.mutable_resources()->MergeFrom(TASK_RESOURCES);
      tasks.push_back(task);
      workqueueMasterIdx_++;
      tasksWaiting_--;
      tasksRunning_++;
    }

    driver->launchTasks(offer.id(), tasks);
  }
}

void
WorkqueueScheduler::offerRescinded(mesos::SchedulerDriver*, mesos::OfferID const&) {
}

void
WorkqueueScheduler::statusUpdate(SchedulerDriver* driver, const TaskStatus &status) {
  std::cout << "Task " <<  status.task_id().value() << ": " << status.state() <<std::endl;
  if (status.state() == TASK_FINISHED) {
    std::cout << "Task " << status.task_id().value() << " finished." << std::endl;
  }
}

void 
WorkqueueScheduler::frameworkMessage(SchedulerDriver* driver,
                                     const ExecutorID& executorId,
                                     const SlaveID& slaveId,
                                     const std::string& data) {

}

void 
WorkqueueScheduler::slaveLost(SchedulerDriver* driver, const SlaveID& sid) {
}

void 
WorkqueueScheduler::executorLost(SchedulerDriver* driver,
                                 const ExecutorID& executorID,
                                 const SlaveID& slaveID,
                                 int status) {
}

void 
WorkqueueScheduler::error(SchedulerDriver* driver, const std::string& message) {
  std::cout << message << std::endl;
}
