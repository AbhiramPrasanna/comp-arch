#!/usr/bin/env python3

import os
import pathlib
import argparse
import random
import string

def other_to_read(line):
    name, table, key, *rest = line.split()
    return f"READ {table} {key} [ <all fields>]\n"

def other_to_insert(line):
    name, table, key, *rest = line.split()
    data = str(key)
    if len(data) < 14:
        data = ("i_" + data + "JUST_FOR_TEST_THIS")[:16]
    else:
        data = "i_" + data[len(data) - 14:]
    return f"INSERT {table} {key} [ field0={data} ]\n"

def other_to_update(line):
    name, table, key, *rest = line.split()
    data = str(key)
    if len(data) < 14:
        data = ("u_" + data + "JUST_FOR_TEST_THIS")[:16]
    else:
        data = "u_" + data[len(data) - 14:]
    return f"UPDATE {table} {key} [ field0={data} ]\n"

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
parser.add_argument('--add_read', '-r', action='store_true', help='Whether to generate read while inserting, default is false')
parser.add_argument('--change2update', action='store_true', help='Whether to generate update operations, default is false')
parser.add_argument('--change2read', action='store_true', help='Whether to generate update operations, default is false')
parser.add_argument('--change2insert', action='store_true', help='Whether to generate update operations, default is false')
parser.add_argument('--input_name', '-i', nargs=None, type=str, help='input file name', required=True)
parser.add_argument('--output_name', '-o', nargs=None, type=str, help='output file name', required=True)
parser.add_argument('--output_path', '-p', nargs=None, type=str, help='Recommend workload/split/', default='workload/split/')
args = parser.parse_args()
output_path = pathlib.Path(args.output_path)
output_path.mkdir(parents=True, exist_ok=True)

# show all args
print("args:", args)
print()

input_file = output_path / args.input_name
output_file = output_path / args.output_name

res = ""
total_num = 0

with open(input_file, 'r') as f:
    lines = f.readlines()
    for line in lines:
        if line.startswith('"') or line.startswith('*') or line.strip() == '':
            continue
        if args.change2read:
            res += other_to_read(line)
            total_num += 1
        elif args.change2update:
            res += other_to_update(line)
            total_num += 1
            if args.add_read:
                res += other_to_read(line)
                total_num += 1
        elif args.change2insert:
            res += other_to_insert(line)
            total_num += 1
            if args.add_read:
                res += other_to_read(line)
                total_num += 1
        else:
            raise ValueError("please use --change2read or --change2update or --change2insert")

result = load_predata(total_num) + res

with open(output_file, "w") as f:
    f.write(result)
