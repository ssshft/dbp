#!/bin/sh
source /inc/version.inc
echo $VERSION
version_home="/opt/version/${VERSION}/dbp/version"
execute_file="./20230814_release/dbwinitialer"
run_path="./20230814_config"

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

file_name=${execute_file##*/}
echo "start running: $execute_file $run_path"
nohup $execute_file $run_path > $run_path/$file_name.out 2>&1 &

sleep 1
ls -all /dev/shm | grep "DBW"

exit 0

