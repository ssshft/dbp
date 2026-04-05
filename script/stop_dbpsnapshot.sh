#!/bin/sh
source /inc/version.inc
echo $VERSION
version_home="/opt/version/${VERSION}/dbp/version"

execute_file="./20230814_release/dbpsnapshot"
run_path="./20230814_config"

command=`ps -ef | grep $execute_file | grep -v grep | awk '{print $2,$9}'`
if [ -z "$command" ]
then
    echo "not process for execute_file: $execute_file"
    exit 0
fi

OLD_IFS="$IFS"
IFS=" "

array=($command)
IFS="$OLD_IFS"

run_pid=''
run_para=''
a=0
for var in ${array[@]}
do
  if [ $(($a%2)) == 0 ]
  then
    run_pid=$var
  else
    run_para=$var
    if [ $run_para = $run_path ]
    then
        echo "kill pid:$run_pid process:$execute_file $run_para"
        sudo kill -9 $run_pid
    fi
  fi
  a=`expr $a + 1`
done

sleep 1
ps -ef | grep "$execute_file"

exit 0
