### About

This is an example [Mesos](https://github.com/apache/mesos)
Framework which allows spawning of
[Workqueue](http://ccl.cse.nd.edu/software/workqueue/) workers on demand
on a Mesos cluster, running them in a docker engine an pulling work from
a specified Workqueue Catalog.

The framework itself works by processing Mesos resource offers and then
looking at all the pending tasks on a Workqueue catalog to decide how
many workers to spawn.

### Compiling

In order to compile you can do:

    cmake "$SOURCEDIR"                         \
          -DCMAKE_INSTALL_PREFIX=$INSTALLROOT  \
          -DPROTOBUF_ROOT=${PROTOBUF_ROOT}     \
          -DMESOS_ROOT=${MESOS_ROOT}           \
          -DBOOST_ROOT=${BOOST_ROOT}           \
          -DGLOG_ROOT=${GLOG_ROOT}

    make -j 20
    make install

where:

- SOURCEDIR is where the sources of this plugin are located.
- INSTALLROOT is where you want the final installation to happen.
- PROTOBUF_ROOT points to your protobuf installation
- MESOS_ROOT points to your mesos installation
- BOOST_ROOT points to your boost installation
- GLOG_ROOT points to your glog installation

### Trying it out

You can start a local Mesos cluster by installing the Docker and Docker
compose on your machine and running:

    docker-compose up

in the source directory. You can connect to the Mesos cluster by opening
your browser on port 5050 of the IP provided by the command:

    docker-machine ip default

You can start the framework by running:

    mesos-workqueue-framework --master $(docker-machine ip default):5050 --catalog $(docker-machine ip default):9097
