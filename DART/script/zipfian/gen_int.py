#!/usr/bin/env python3

import os
import math
import pathlib
import argparse
import random
import string

import cpp_zipfian

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
default_zipf_para = 0.99
run_write_partial_a = 0.5
run_write_partial_b = 0.05
run_write_partial_d = 0.05
ml_load_paritial = 1 / 6

parser = argparse.ArgumentParser(description='Split YCSB workload into multiple files and send to different compute machines')
parser.add_argument('--input', nargs=None, type=str, default="./workload/data/c_load")
parser.add_argument('--max', nargs=None, type=int, default=60_000_000)
parser.add_argument('--output_path', nargs=None, type=str, help='Recommend workload/data/', default='workload/data/')
parser.add_argument('--zipf_para', nargs=None, type=float, help='zipf parameter', default=default_zipf_para)
parser.add_argument('--use_ml', action='store_true', help='Whether to use ml_load and ml_run files, default is false', default=False)
args = parser.parse_args()
input_file = pathlib.Path(args.input)
output_path = pathlib.Path(args.output_path)
output_path.mkdir(parents=True, exist_ok=True)

zipf_para = args.zipf_para
use_ml = args.use_ml

if not input_file.exists():
    print(f"{input_file} not exists!")
    exit(1)

# Count valid INSERT lines instead of total lines
print("Counting valid INSERT lines...")
r = os.popen(f'grep -c "^INSERT usertable" {input_file}')
available_total_lines = int(r.read().strip())
print(f"{available_total_lines = }")
for_d_then_max = int(math.floor(available_total_lines / (1 + run_write_partial_d)))
print(f"{for_d_then_max = }")


# show all args
print("args:", args)
print()


runa_file = output_path / "a_run_9"




ascii_size = len(string.ascii_letters)

def random_str():
    r = ""
    for _ in range(random.randint(8, 20)):
        r += string.ascii_letters[random.randint(0, ascii_size - 1)]
    return r

def data_str(prefix, data):
    if len(data) < 14:
        return (prefix + data + "HELLOWORLDTHERE!!!")[:16]
    else:
        return prefix + data[len(data) + len(prefix) - 16:]

# load file
if use_ml:
    print("mm_load, ml_load & ml_run begin.")
else:
    print("mm_load begin.")
result = ""
if use_ml:
    result_ml_load = ""
    result_ml_run = ""
input_f = open(input_file, 'r')
keys = []
new_keys = []

now_total_num = 0
valid_lines_count = 0


while valid_lines_count < available_total_lines:
    line = input_f.readline()
    if not line: # handle end of file
        break
    
    # Skip properties header lines
    if line.startswith('*****************') or line.startswith('"') or line.strip() == '':
        continue
    
    # Extract user key from INSERT usertable user123456 [ field0=...] format
    if line.startswith('INSERT usertable'):
        parts = line.strip().split()
        if len(parts) >= 3:
            key = parts[2]  # This should be user123456, keep the full key with "user" prefix
            valid_lines_count += 1
            if valid_lines_count % 100_000 == 0:
                print(f"load(pre) = {valid_lines_count}")
        else:
            continue
    else:
        continue

    keys.append(key)
    if (now_total_num < 100):
        print(key)
    now_total_num += 1
print(now_total_num)

total_num = min(now_total_num, args.max)

print("Generating Zipfian distribution using C++ module...")
print(f"total num: {total_num}")
if total_num > 0:
    # seed = random.randint(0, (1 << 48) - 1)
    indices = cpp_zipfian.generate(count=total_num, n=total_num, theta=zipf_para, seed=0)
else:
    indices = []
print("Generation done.")


# run a
print("a_run begin.")
result = ""
random.shuffle(keys)
update_num = int(run_write_partial_a * total_num)
search_num = total_num - update_num
now_update = 0
now_search = 0
for num, i in enumerate(indices):
    if num % 100_000 == 0:
        print(f"ma_run = {num}")
    if now_update < update_num and random.random() < run_write_partial_a:
        result += update_log(keys[i], data_str("u_", keys[i]))
        now_update += 1
    elif now_search < search_num:
        result += search_log(keys[i])
        now_search += 1
    elif now_update < update_num:
        result += update_log(keys[i], data_str("u_", keys[i]))
        now_update += 1
result = load_predata(total_num) + result
with open(runa_file, "w") as load:
    load.write(result)
print(f"update num: {now_update}, search num: {now_search}")
print("a_run done.")



