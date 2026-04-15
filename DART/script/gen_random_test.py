#!/usr/bin/env python3

import os
import pathlib
import argparse
import random
import string

def read_log(key):
    return f"READ usertable {key} [ <all fields>]\n"

def insert_log(key):
    data = str(key)
    if len(data) < 14:
        data = ("i_" + data + "JUST_FOR_TEST_THIS")[:16]
    else:
        data = "i_" + data[len(data) - 14:]
    return f"INSERT usertable {key} [ field0={data} ]\n"

def update_log(key):
    data = str(key)
    if len(data) < 14:
        data = ("u_" + data + "JUST_FOR_TEST_THIS")[:16]
    else:
        data = "u_" + data[len(data) - 14:]
    return f"UPDATE usertable {key} [ field0={data} ]\n"

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
parser.add_argument('--total', '-t', nargs=None, type=int, help='key number', default=1000)
parser.add_argument('--str', '-s', action='store_true', help='Whether to generate string operations, default is u64')
parser.add_argument('--add_read', '-r', action='store_true', help='Whether to generate read while inserting, default is false')
parser.add_argument('--update', '-u', action='store_true', help='Whether to generate update operations, default is false')
parser.add_argument('--output_path', '-p', nargs=None, type=str, help='Recommend workload/split/', default='workload/split/')
args = parser.parse_args()
output_path = pathlib.Path(args.output_path)
output_path.mkdir(parents=True, exist_ok=True)

# show all args
print("args:", args)
print()

if args.str:
    load_file = output_path / "mr_load"
    run_file = output_path / "mr_run"
else:
    load_file = output_path / "ur_load"
    run_file = output_path / "ur_run"

print(f"{load_file = }")
print(f"{run_file = }")

ascii_size = len(string.ascii_letters)

total_num = args.total

result = ""
sample_shown = False

def random_str():
    r = ""
    for _ in range(random.randint(8, 20)):
        r += string.ascii_letters[random.randint(0, ascii_size - 1)]
    return r

def random_u64():
    return "user" + str(random.randint(0, 18446744073709551615))

keys = []

for i in range(total_num):
    if args.str:
        key = random_str()
    else:
        key = random_u64()
    result += insert_log(key)
    if args.add_read:
        result += read_log(key)
    keys.append(key)
    if not sample_shown:
        print("<sample load key>")
        print(result.strip())
        sample_shown = True

result = load_predata(total_num * 2) + result

with open(load_file, "w") as load:
    load.write(result)

result = ""

sample_shown = False

for key in keys:
    if args.update:
        result += update_log(key)
    else:
        result += read_log(key)
    if not sample_shown:
        print("<sample run key>")
        print(result.strip())
        sample_shown = True

result = load_predata(total_num) + result

with open(run_file, "w") as load:
    load.write(result)

print("Done.")
