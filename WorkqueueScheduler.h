#include <mesos/scheduler.hpp>

size_t updateTasksWaiting(void *ptr, size_t size, size_t nmemb, void *stream);

struct WorkqueueMasterInfo {
  std::string hostname;
  std::string port;
};

struct WorkqueueVolumeInfo {
  std::string host;
  std::string container;
  bool readOnly;
};

class WorkqueueScheduler : public mesos::Scheduler {
public:
  WorkqueueScheduler(const std::string &catalog, 
                     const std::string &docker,
                     const std::vector<WorkqueueVolumeInfo> &volumes,
                     const mesos::ExecutorInfo &executor);

  virtual void registered(mesos::SchedulerDriver* driver,
                          const mesos::FrameworkID& frameworkId,
                          const mesos::MasterInfo& masterInfo);
  virtual void reregistered(mesos::SchedulerDriver* driver,
                          const mesos::MasterInfo& masterInfo);
  virtual void disconnected(mesos::SchedulerDriver* driver);
  virtual void resourceOffers(mesos::SchedulerDriver* driver,
                              const std::vector<mesos::Offer>& offers);
  virtual void offerRescinded(mesos::SchedulerDriver* driver,
                              const mesos::OfferID& offerId);
  virtual void statusUpdate(mesos::SchedulerDriver* driver,
                            const mesos::TaskStatus& status);
  virtual void frameworkMessage(mesos::SchedulerDriver* driver,
                                const mesos::ExecutorID& executorId,
                                const mesos::SlaveID& slaveId,
                                const std::string& data);
  virtual void slaveLost(mesos::SchedulerDriver* driver,
                         const mesos::SlaveID& slaveId);
  virtual void executorLost(mesos::SchedulerDriver* driver,
                            const mesos::ExecutorID& executorId,
                            const mesos::SlaveID& slaveId,
                            int status);
  virtual void error(mesos::SchedulerDriver* driver,
                     const std::string& message);
private:
  mesos::FrameworkID id_;
  mesos::MasterInfo master_;
  std::string catalog_;
  std::string docker_;
  std::vector<WorkqueueVolumeInfo> volumes_;
  mesos::ExecutorInfo workerInfo_;
  size_t      workqueueMasterIdx_;
  std::vector<WorkqueueMasterInfo> workqueueMasterInfos_;
};
