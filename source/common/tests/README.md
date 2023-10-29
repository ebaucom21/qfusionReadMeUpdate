```shell script
# CMAKE_PREFIX_PATH should be specified unless there is a system-wide Qt distribution.
# Discover an actual path on your machine. This one is for example.
$ cmake DCMAKE_PREFIX_PATH=/home/********/Qt/5.13.2/gcc_64 .
$ make && ./commontest
```