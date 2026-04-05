#!/bin/sh
source /inc/version.inc
echo $VERSION
version_home="/opt/version/${VERSION}/dbp/version"

stop_script="/opt/version/${VERSION}/dbp/script/stop_dbpsnapshot.sh"

pid_path="/run/sig/dbpsnapshot.pid"

execute_file="./20230814_release/dbpsnapshot"
run_path="./20230814_config"

${stop_script}

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

cur_date=$(date -d "now" +%Y%m%d)
file_name=${execute_file##*/}
echo "start running: $execute_file $run_path"
nohup $execute_file $run_path > /dev/null 2>&1 &
echo $! > ${pid_path}
cat ${pid_path}

sleep 1
ps -ef | grep "$execute_file"

exit 0
