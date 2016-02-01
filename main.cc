#include <iostream>
#include <signal.h>
#include <getopt.h>
#include "WorkqueueScheduler.h"
#include <regex>
#include <vector>
#include <tuple>

using namespace mesos;

const char *USAGE = "Usage: workqueue-mesos-framework"          \
                    " --master <url>"                           \
                    " --docker <docker image>"                  \
                    " [--volume] <host path>:<container path>"  \
                    " --catalog <url>[:<port>]\n";

MesosSchedulerDriver* schedulerDriver;

static void shutdown()
{
  printf("Mesos Workqueue is shutting down.\n");
}


static void SIGINTHandler(int signum)
{
  if (schedulerDriver != NULL) {
    shutdown();
    schedulerDriver->stop();
  }
  delete schedulerDriver;
  exit(0);
}

std::vector<std::string> split(const std::string& input, const std::string& regex) {
    // passing -1 as the submatch index parameter performs splitting
    std::regex re(regex);
    std::sregex_token_iterator
        first{input.begin(), input.end(), re, -1},
        last;
    return {first, last};
}

const char *DEFAULT_WORKQUEUE_MESOS_CATALOG = "localhost:9097";
const char *DEFAULT_WORKQUEUE_MESOS_MASTER = "localhost:5050";
const char *DEFAULT_WORKQUEUE_DOCKER = "alisw/slc6-builder";

static struct option options[] = {
  { "help", no_argument, NULL, 'h' },
  { "master", required_argument, NULL, 'm' },
  { "catalog", required_argument, NULL, 'C' },
  { "docker", required_argument, NULL, 'D' },
  { "volume", required_argument, NULL, 'v' },
  { NULL, 0, NULL, 0 }
};

int main(int argc, char **argv) {
  const char *defaultCatalog = getenv("WORKQUEUE_MESOS_CATALOG");
  const char *defaultMaster = getenv("WORKQUEUE_MESOS_MASTER");
  const char *defaultDocker = getenv("WORKQUEUE_MESOS_DOCKER");
  std::string catalog = defaultCatalog ? defaultCatalog : DEFAULT_WORKQUEUE_MESOS_CATALOG;
  std::string master = defaultMaster ? defaultMaster : DEFAULT_WORKQUEUE_MESOS_MASTER;
  std::string docker = defaultDocker ? defaultDocker : DEFAULT_WORKQUEUE_DOCKER;
  std::vector<WorkqueueVolumeInfo> volumes;

  while (true) {
    int option_index;
    int c = getopt_long(argc, argv, "hm:C:D:", options, &option_index);
     
    if (c == -1)
      break;
    switch(c)
    {
      case 'h':
        std::cerr << USAGE << std::endl;
        break;
      case 'm':
        master = optarg;
        break;
      case 'C':
        catalog = optarg;
        break;
      case 'D':
        docker = optarg;
        break;
      case 'v':
      {
        auto r = split(optarg, ":");
        if (r.size() == 1)
          r.push_back(r[0]);
        if (r.size() != 2)
        {
          std::cerr << "Error while passing argument to option -v: " << optarg << std::endl; 
          exit(1);
        }
        volumes.push_back(WorkqueueVolumeInfo{r[0], r[1]});
      }
        break;
      case '?':
        /* getopt_long already printed an error message. */
        break;
      default:
        abort ();
    }
  }

  ExecutorInfo worker;
  worker.mutable_executor_id()->set_value("Worker");
  worker.set_name("Workqueue Worker Executor");
  worker.mutable_command()->set_shell(false);

  WorkqueueScheduler scheduler(catalog, docker, volumes, worker);

  FrameworkInfo framework;
  framework.set_user(""); // Have Mesos fill in the current user.
  framework.set_name("Mesos Workqueue Framework");
  //framework.set_role(role);
  framework.set_principal("workqueue");

  // Set up the signal handler for SIGINT for clean shutdown.
  struct sigaction action;
  action.sa_handler = SIGINTHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGINT, &action, NULL);

  schedulerDriver = new MesosSchedulerDriver(&scheduler, framework, master);
  int status = schedulerDriver->run() == DRIVER_STOPPED ? 0 : 1;

  // Ensure that the driver process terminates.
  schedulerDriver->stop();

  shutdown();

  delete schedulerDriver;

  return status;
}
