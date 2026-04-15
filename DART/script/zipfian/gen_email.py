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
parser.add_argument('--input', nargs=None, type=str, default="email_filtered.txt")
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

r = os.popen('wc -l ' + str(input_file))
available_total_lines = int(r.read().split()[0])
print(f"{available_total_lines = }")
for_d_then_max = int(math.floor(available_total_lines / (1 + run_write_partial_d)))
print(f"{for_d_then_max = }")

total_num = min(for_d_then_max, args.max)
print(f"{total_num = }")

d_insert_num = int(run_write_partial_d * total_num)
if use_ml:
    ml_load_num = int(total_num * ml_load_paritial)
    ml_run_num = total_num - ml_load_num

# show all args
print("args:", args)
print()

load_file = output_path / "mm_load"
runa_file = output_path / "ma_run"
runb_file = output_path / "mb_run"
runc_file = output_path / "mc_run"
rund_file = output_path / "md_run"
rune_file = output_path / "me_run"

if use_ml:
    load_small_insert_file = output_path / "ml_load"
    run_big_insert_file = output_path / "ml_run"

print(f"{load_file = }, attention double m for all (ma, mb, ...) run files")
print(f"run_file = {runa_file}, {runb_file}, {runc_file}, {rund_file}, {rune_file}")
if use_ml:
    print(f"{load_small_insert_file = }, for {run_big_insert_file = } files") # type: ignore
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
new_d_insert_num = 0
if use_ml:
    now_ml_load_num = 0
    now_ml_run_num = 0
for i in range(int(total_num + d_insert_num)):
    if i % 100_000 == 0:
        print(f"load(pre) = {i}")
    line = input_f.readline()
    if not line: # handle end of file
        break
    key = line.strip().split('@')
    name = key[0]
    domain = key[1].split('.')
    domain.reverse()
    domain = '.'.join(domain)
    key = domain + '@' + name
    if new_d_insert_num < d_insert_num and random.random() < run_write_partial_d:
        new_keys.append(key)
        new_d_insert_num += 1
    elif now_total_num < total_num:
        keys.append(key)
        now_total_num += 1
    elif new_d_insert_num < d_insert_num:
        new_keys.append(key)
        new_d_insert_num += 1

random.shuffle(keys)

# Ensure we have enough keys if input file was smaller than expected
total_num = now_total_num
if use_ml:
    ml_load_num = int(total_num * ml_load_paritial)
    ml_run_num = total_num - ml_load_num
print(f"Adjusted total_num = {total_num}")


for i, key in enumerate(keys):
    if i % 100_000 == 0:
        print(f"load = {i}")
    value = data_str("i_", key)
    result += insert_log(key, value)
    if use_ml:
        if now_ml_load_num < ml_load_num and random.random() < ml_load_paritial: # type: ignore
            now_ml_load_num += 1 # type: ignore
            result_ml_load += insert_log(key, value) # type: ignore
        elif now_ml_run_num < ml_run_num: # type: ignore
            now_ml_run_num += 1 # type: ignore
            result_ml_run += insert_log(key, value) # type: ignore
        elif now_ml_load_num < ml_load_num: # type: ignore
            now_ml_load_num += 1 # type: ignore
            result_ml_load += insert_log(key, value) # type: ignore

result = load_predata(now_total_num) + result
if use_ml:
    result_ml_load = load_predata(now_ml_load_num) + result_ml_load # type: ignore
    result_ml_run = load_predata(now_ml_run_num) + result_ml_run # type: ignore
with open(load_file, "w") as load:
    load.write(result)
if use_ml:
    with open(load_small_insert_file, "w") as load:  # type: ignore
        load.write(result_ml_load) # type: ignore
    with open(run_big_insert_file, "w") as load:  # type: ignore
        load.write(result_ml_run) # type: ignore
input_f.close()
if use_ml:
    print(f"{now_total_num = }, {new_d_insert_num = }, {now_ml_load_num = }, {now_ml_run_num = }") # type: ignore
    print("mm_load, ml_load & ml_run done.")
else:
    print(f"{now_total_num = }, {new_d_insert_num = }")
    print("mm_load done.")


if use_ml:
    del result_ml_load # type: ignore
    del result_ml_run # type: ignore


print("Generating Zipfian distribution using C++ module...")
if total_num > 0:
    # seed = random.randint(0, (1 << 48) - 1)
    indices = cpp_zipfian.generate(count=total_num, n=total_num, theta=zipf_para, seed=0)
else:
    indices = []
print("Generation done.")


# run a
print("ma_run begin.")
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
print("ma_run done.")

# run b
print("mb_run begin.")
result = ""
update_num = int(run_write_partial_b * total_num)
search_num = total_num - update_num
now_update = 0
now_search = 0
for num, i in enumerate(indices):
    if num % 100_000 == 0:
        print(f"mb_run = {num}")
    if now_update < update_num and random.random() < run_write_partial_b:
        result += update_log(keys[i], data_str("u_", keys[i]))
        now_update += 1
    elif now_search < search_num:
        result += search_log(keys[i])
        now_search += 1
    elif now_update < update_num:
        result += update_log(keys[i], data_str("u_", keys[i]))
        now_update += 1
result = load_predata(total_num) + result
with open(runb_file, "w") as load:
    load.write(result)
print(f"update num: {now_update}, search num: {now_search}")
print("mb_run done.")

# run c
print("mc_run begin.")
result = ""
for num, i in enumerate(indices):
    if num % 100_000 == 0:
        print(f"mc_run = {num}")
    result += search_log(keys[i])
result = load_predata(total_num) + result
with open(runc_file, "w") as load:
    load.write(result)
print(f"search num: {total_num}")
print("mc_run done.")


# run d
print("md_run begin.")
result = ""
random.shuffle(new_keys)
insert_num = int(run_write_partial_d * total_num)
search_num = total_num - insert_num 
now_insert = 0
now_search = 0
for num, i in enumerate(indices):
    if num % 100_000 == 0:
        print(f"md_run = {num}")
    if now_insert < len(new_keys) and now_insert < insert_num and random.random() < run_write_partial_d:
        result += insert_log(new_keys[now_insert], data_str("n_", new_keys[now_insert]))
        now_insert += 1
    elif now_search < search_num:
        result += search_log(keys[i])
        now_search += 1
    elif now_insert < len(new_keys) and now_insert < insert_num:
        result += insert_log(new_keys[now_insert], data_str("n_", new_keys[now_insert]))
        now_insert += 1

result = load_predata(total_num) + result
with open(rund_file, "w") as load:
    load.write(result)
print(f"insert num: {now_insert}, search num: {now_search}")
print("md_run done.")
