#!/bin/sh
version_home="/opt/version/sig/versions"
execute_file="./v1.1.0-20230522/dbsinitialer"
run_path="./20230505_REALTEST"

cd ${version_home}
if [ ! -f $execute_file ]
then
    echo "$execute_file not exist!"
    exit 0
fi

if [ ! -d $run_path ]
then
    echo "$run_path not a directory!"
    exit 0
fi

file_name=${run_path##*/}
echo "start running: $execute_file $run_path"
nohup $execute_file $run_path > $run_path/$file_name.out 2>&1 &

sleep 1
ls -all /dev/shm | grep "DBS"

exit 0

