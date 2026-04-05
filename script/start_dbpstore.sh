#!/bin/sh

version_home="/opt/version/sig/versions"
stop_script="/opt/version/sig/versions/script/stop_dbpstore.sh"
pid_path="/run/sig/dbpstore.pid"

execute_file="./v1.1.0-20230522/dbpstore"
run_path="./20230505_REALTEST"

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