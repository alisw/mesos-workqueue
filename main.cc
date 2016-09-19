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
                    " [--user <user name>]"                     \
                    " [--framework_name <framework name>]"      \
                    " [--role <mesos role>]"                    \
                    " [--principal <mesos principal>]"          \
                    " [--cores <number of cores>]"              \
                    " [--memory <MB>]"                          \
                    " [--volume <host path>:<container path>]"  \
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
const char *DEFAULT_WORKQUEUE_USER = ""; // Have Mesos fill in the current user.
const char *DEFAULT_WORKQUEUE_FRAMEWORK_NAME = "Mesos Workqueue Framework";
const char *DEFAULT_WORKQUEUE_ROLE = "";
const char *DEFAULT_WORKQUEUE_PRINCIPAL = "workqueue";
const char *DEFAULT_WORKQUEUE_CORES = "1";
const char *DEFAULT_WORKQUEUE_MEMORY = "1024";
const char *DEFAULT_WORKQUEUE_PRIVILEGED = "false";

static struct option options[] = {
  { "help", no_argument, NULL, 'h' },
  { "master", required_argument, NULL, 'm' },
  { "catalog", required_argument, NULL, 'C' },
  { "docker", required_argument, NULL, 'D' },
  { "user", required_argument, NULL, 'u' },
  { "framework_name", required_argument, NULL, 'N' },
  { "role", required_argument, NULL, 'r' },
  { "principal", required_argument, NULL, 'P' },
  { "volume", required_argument, NULL, 'v' },
  { "memory", required_argument, NULL, 'M' },
  { "cores", required_argument, NULL, 'c' },
  { "privileged", no_argument, NULL, 'p' },
  { NULL, 0, NULL, 0 }
};

void die(const char *s)
{
  std::cerr << s << std::endl;
  exit(1);
}

int main(int argc, char **argv) {
  const char *defaultCatalog = getenv("WORKQUEUE_MESOS_CATALOG");
  const char *defaultMaster = getenv("WORKQUEUE_MESOS_MASTER");
  const char *defaultDocker = getenv("WORKQUEUE_MESOS_DOCKER");
  const char *defaultUser = getenv("WORKQUEUE_MESOS_USER");
  const char *defaultFrameworkName = getenv("WORKQUEUE_MESOS_FRAMEWORK_NAME");
  const char *defaultRole = getenv("WORKQUEUE_MESOS_ROLE");
  const char *defaultPrincipal = getenv("WORKQUEUE_MESOS_PRINCIPAL");
  const char *defaultCores = getenv("WORKQUEUE_MESOS_CORES");
  const char *defaultMemory = getenv("WORKQUEUE_MESOS_MEMORY");
  const char *defaultPrivileged = getenv("WORKQUEUE_MESOS_PRIVILEGED");

  std::string catalog = defaultCatalog ? defaultCatalog : DEFAULT_WORKQUEUE_MESOS_CATALOG;
  std::string master = defaultMaster ? defaultMaster : DEFAULT_WORKQUEUE_MESOS_MASTER;
  std::string docker = defaultDocker ? defaultDocker : DEFAULT_WORKQUEUE_DOCKER;
  std::string user = defaultUser ? defaultUser : DEFAULT_WORKQUEUE_USER;
  std::string frameworkName = defaultFrameworkName ? defaultFrameworkName : DEFAULT_WORKQUEUE_FRAMEWORK_NAME;
  std::string role = defaultRole ? defaultRole : DEFAULT_WORKQUEUE_ROLE;
  std::string principal = defaultPrincipal ? defaultPrincipal : DEFAULT_WORKQUEUE_PRINCIPAL;
  std::string cores = defaultCores ? defaultCores : DEFAULT_WORKQUEUE_CORES;
  std::string memory = defaultMemory ? defaultMemory : DEFAULT_WORKQUEUE_MEMORY;
  std::string privileged = defaultPrivileged ? defaultPrivileged : DEFAULT_WORKQUEUE_PRIVILEGED;
  std::vector<WorkqueueVolumeInfo> volumes;

  while (true) {
    int option_index;
    int c = getopt_long(argc, argv, "hpm:C:D:u:N:r:P:c:M:", options, &option_index);

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
      case 'u':
        user = optarg;
        break;
      case 'N':
        frameworkName = optarg;
        break;
      case 'r':
        role = optarg;
        break;
      case 'P':
        principal = optarg;
        break;
      case 'v':
      {
        auto r = split(optarg, ":");
        if (r.size() == 1)
          r.push_back(r[0]);
        if (r.size() == 2)
          r.push_back("RW");

        if (r.size() != 3)
        {
          std::cerr << "Error while passing argument to option -v: " << optarg << std::endl;
          exit(1);
        }
        volumes.push_back(WorkqueueVolumeInfo{r[0], r[1], r[2] == "RO"});
      }
        break;
      case 'c':
        cores = optarg;
        break;
      case 'M':
        memory = optarg;
        break;
      case 'p':
        privileged = "true";
        break;
      case '?':
        /* getopt_long already printed an error message. */
        break;
      default:
        abort ();
    }
  }

  char *err;

  int coresCount = strtol(cores.c_str(), &err, 10);
  if (*err != 0)
    die("Error while parsing -c / --cores.");

  int memoryCount = strtol(memory.c_str(), &err, 10);
  if (*err != 0)
    die("Error while parsing -M / --memory.");

  bool privilegedOpt = (privileged == "true");

  ExecutorInfo worker;
  worker.mutable_executor_id()->set_value("Worker");
  worker.set_name("Workqueue Worker Executor");
  worker.mutable_command()->set_shell(false);

  WorkqueueScheduler scheduler(catalog, docker, volumes, worker, coresCount, memoryCount, privilegedOpt);

  FrameworkInfo framework;
  framework.set_user(user);
  framework.set_name(frameworkName);
  framework.set_principal(principal);
  if (!role.empty())
    framework.set_role(role);

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
