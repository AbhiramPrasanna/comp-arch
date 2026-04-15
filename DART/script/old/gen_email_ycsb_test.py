#!/usr/bin/env python3

import os
import pathlib
import argparse
import random
import string
import numpy as np

def insert_log(key, value):
    return f"INSERT usertable {key} [ field0={value} ]\n"

def update_log(key, value):
    return f"UPDATE usertable {key} [ field0={value} ]\n"

def search_log(key):
    return f"READ usertable {key} [ <all fields>]\n"

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
"workload"="costume.email"
"recordcount"="{total_num}"
"updateproportion"="0"
"db"="site.ycsb.BasicDB"
"operationcount"="{total_num}"
**********************************************
"""


parser = argparse.ArgumentParser(description='Split YCSB workload into multiple files and send to different compute machines')
parser.add_argument('--input', nargs=None, type=str, default="email_filtered.txt")
parser.add_argument('--max', nargs=None, type=int, default=1000)
parser.add_argument('--output_path', nargs=None, type=str, help='Recommend workload/split/', default='workload/split/')
args = parser.parse_args()
input_file = pathlib.Path(args.input)
output_path = pathlib.Path(args.output_path)
output_path.mkdir(parents=True, exist_ok=True)

if not input_file.exists():
    print(f"{input_file} not exists!")
    exit(1)

r = os.popen('wc -l ' + str(input_file))
available_total_lines = int(r.read().split()[0])
print(f"{available_total_lines = }")

total_num = min(available_total_lines, args.max)
print(f"{total_num = }")

# show all args
print("args:", args)
print()

load_file = output_path / "mt_load"
run_file = output_path / "mt_run"

print(f"{load_file = }")
print(f"{run_file = }")

ascii_size = len(string.ascii_letters)

def random_str():
    r = ""
    for _ in range(random.randint(8, 20)):
        r += string.ascii_letters[random.randint(0, ascii_size - 1)]
    return r

# insert
result = ""
input_f = open(input_file, 'r')
keys = []
for i in range(total_num):
    if (i + 1) % 100_000 == 0:
        print(f"i = {i + 1}")
    line = input_f.readline()
    key = line.strip().split('@')
    name = key[0]
    domain = key[1].split('.')
    domain.reverse()
    domain = '.'.join(domain)
    key = domain + '@' + name
    result += insert_log(key, random_str())
    result += search_log(key)
    keys.append(key)
result = load_predata(total_num * 2) + result
with open(load_file, "w") as load:
    load.write(result)
input_f.close()

# important
# uniform random read
result = ""
for key in keys:
    result += search_log(key)
result = load_predata(total_num) + result
with open(run_file, "w") as load:
    load.write(result)

