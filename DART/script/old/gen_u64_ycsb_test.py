#!/usr/bin/env python3

import os
import pathlib
import argparse

def insert_log_user(key):
    data = str(key)
    if len(data) < 16:
        data = (data + "JUST_FOR_TEST_THIS")[:16]
    else:
        data = data[len(data) - 16:]
    return f"INSERT usertable user{key} [ field0={data} ]\n"

def search_log_user(key):
    return f"READ usertable user{key} [ <all fields>]\n"

def load_predata(total_num):
    return f"""***************** properties *****************
"insertproportion"="0"
"fieldcount"="1"
"fieldlength"="16"
"readproportion"="1"
"scanproportion"="0"
"readallfields"="true"
"dotransactions"="false"
"status"="true"
"requestdistribution"="zipfian"
"workload"="site.ycsb.workloads.CoreWorkload"
"recordcount"="{total_num}"
"updateproportion"="0"
"db"="site.ycsb.BasicDB"
"operationcount"="{total_num}"
**********************************************
"""


parser = argparse.ArgumentParser(description='Split YCSB workload into multiple files and send to different compute machines')
parser.add_argument('--output_path', nargs=None, type=str, help='Recommend workload/split/', default='workload/split/')
args = parser.parse_args()
output_path = pathlib.Path(args.output_path)
output_path.mkdir(parents=True, exist_ok=True)

# show all args
print("args:", args)
print()

load_file = output_path / "t_load"
run_file = output_path / "t_run"

print(f"{load_file = }")
print(f"{run_file = }")

result = ""

front_min = 0
front_max = 255
front_step = 1
front_len = (front_max - front_min - 1) // front_step + 1
front_mag = 256 ** 6

last_min = 0
last_max = 256
last_step = 15
last_len = (last_max - last_min - 1) // last_step + 1
last_mag = 1


normal_base = 0x1234567890abcdef


for front_byte in range(front_min, front_max, front_step):
    for last_byte in range(last_min, last_max, last_step):
        key = last_byte * last_mag + front_byte * front_mag + normal_base
        result += insert_log_user(key)
        result += search_log_user(key)

result = load_predata(front_len * last_len * 2) + result

with open(load_file, "w") as load:
    load.write(result)

result = ""

for front_byte in range(front_min, front_max, front_step):
    for last_byte in range(last_min, last_max, last_step):
        key = last_byte * last_mag + front_byte * front_mag + normal_base
        result += search_log_user(key)

result = load_predata(front_len * last_len) + result

with open(run_file, "w") as load:
    load.write(result)

print("Done.")
