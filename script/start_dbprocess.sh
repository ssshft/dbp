#!/bin/sh
source /inc/version.inc
echo $VERSION
version_home="/opt/version/${VERSION}/dbp/version"

stop_script="/opt/version/${VERSION}/dbp/script/stop_dbprocess.sh"
pid_path="/run/dbprocess.pid"

execute_file="./20230814_release/dbprocess"
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
nohup $execute_file $run_path > $run_path/$cur_date_$file_name.out 2>&1 &
echo $! > ${pid_path}
cat ${pid_path}

sleep 1
ps -ef | grep "$execute_file"

exit 0
