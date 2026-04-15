#!/usr/bin/env python3

import os
import math
import pathlib
import argparse
import random
import string
import numpy as np
import re

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

# important
zipf_para = 1.2
run_write_partial_a = 0.5
run_write_partial_b = 0.05
run_write_partial_d = 0.05
ml_load_paritial = 1 / 6

parser = argparse.ArgumentParser(description='Split YCSB workload into multiple files and send to different compute machines')
parser.add_argument('--input', nargs=None, type=str, default="email_filtered.txt")
parser.add_argument('--max', nargs=None, type=int, default=60_000_000)
parser.add_argument('--output_path', nargs=None, type=str, help='Recommend workload/data/', default='workload/data/')
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
for_d_then_max = int(math.floor(available_total_lines / (1 + run_write_partial_d)))
print(f"{for_d_then_max = }")

total_num = min(for_d_then_max, args.max)
print(f"{total_num = }")

d_insert_num = int(run_write_partial_d * total_num)
ml_load_num = int(total_num * ml_load_paritial)
ml_run_num = total_num - ml_load_num

# show all args
print("args:", args)
print()

load_file = output_path / "email_load"
runa_file = output_path / "email_s99"
runb_file = output_path / "mb_run"
runc_file = output_path / "mc_run"
rund_file = output_path / "md_run"
rune_file = output_path / "me_run"

load_small_insert_file = output_path / "ml_load"
run_big_insert_file = output_path / "ml_run"

print(f"{load_file = }, attention double m for all (ma, mb, ...) run files")
print(f"run_file = {runa_file}, {runb_file}, {runc_file}, {rund_file}, {rune_file}")
print(f"{load_small_insert_file = }, for {run_big_insert_file = } files")

ascii_size = len(string.ascii_letters)

def random_str():
    r = ""
    for _ in range(random.randint(8, 20)):
        r += string.ascii_letters[random.randint(0, ascii_size - 1)]
    return r

keys = []
input_f = open("/home/bowen/prheart/workload/data/a_order_0_run", 'r')
result = ""
key_num = 60000000
count = 0
for i in range(key_num):
    line = input_f.readline()
    print(line)
    if (i == 100):
        break
        # print(line)



