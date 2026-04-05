#!/bin/sh

version_home="/opt/version/joey"
pid_path="/run/ddbmonitor.pid"

execute_file="./ddbmonitor.py"

cd ${version_home}
if [ ! -f $execute_file ]
then
    echo "$execute_file not exist!"
    exit 0
fi

cur_date=$(date -d "now" +%Y%m%d)
file_name=${execute_file##*/}
echo "start running: $execute_file"
nohup python $execute_file > ddbm.out 2>&1 &
echo $! > ${pid_path}
cat ${pid_path}

sleep 1
ps -ef | grep "$execute_file"

exit 0