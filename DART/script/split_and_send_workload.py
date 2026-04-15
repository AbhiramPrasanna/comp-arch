#!/usr/bin/env python3

import os
import pathlib
import argparse


class attrdict(dict):
    __getattr__ = dict.__getitem__
    __setattr__ = dict.__setitem__


START_1 = "***************** properties *****************"
START_2 = "**********************************************"
START_PREFIX = "*******"

# line <--> MiB
# 15 klines = 1 MiB
# 1 Mlines = 66.67 MiB
MiB_TO_LINES = 15000


parser = argparse.ArgumentParser(description='Split YCSB workload into multiple files and send to different compute machines')
parser.add_argument('--project_parent', nargs=None, type=str, help='The parent directory of project, default is ~', default='~')
parser.add_argument('--input_path', nargs=None, type=str, help='Recommend workload/data/', default='workload/data/')
parser.add_argument('--split_path', nargs=None, type=str, help='Recommend workload/split/', default='workload/split/')
parser.add_argument('--ip_prefix', nargs=None, type=str, help='Something like 192.168.0.', default='192.168.98.')
parser.add_argument('--inputs', nargs="+", type=str, help='Several names of input files in ${input_path}, usually xx_run or xx_load')
parser.add_argument('--outputs', nargs="+", type=str, help='Several names of output files in ${split_path}, usually xx_run_xx or xx_load_xx, default is same as inputs', default=None)
parser.add_argument('--kline', nargs=None, type=float, help='kilo-lines of the total workload, can be float (must smaller than the input file)', default=None)
parser.add_argument('--ips', nargs="+", type=str, help='Several IPs as the targets to send the output files to, the order of IPs determines which part of the slices to send. Files send to ${split_path}/xx_run_xx|xx_load_xx')
args = parser.parse_args()
args.input_path = pathlib.Path(args.input_path)
args.split_path = pathlib.Path(args.split_path)

# show all args
print("args:", args)
print()

tmp_dir = args.input_path / "tmp/"
tmp_dir.mkdir(parents=True, exist_ok=True)

# split the input file into multiple files
for num, input in enumerate(args.inputs):

    output = args.outputs[num] if args.outputs is not None else input

    print("\033[;34m" + f"Processing {input}..." + "\033[0m")

    input_file = args.input_path / str(input)
    split_file = args.split_path / str(output)
    print(f"{input_file = }")
    print(f"{split_file = }")
    print(f"{tmp_dir = }")

    # use wc -l to get the number of lines in the input_file file
    r = os.popen('wc -l ' + str(input_file))
    predicted_total_lines = int(r.read().split()[0])
    print(f"{predicted_total_lines = }")

    # get metadata
    input_f = open(input_file, 'r')
    metadata = attrdict()
    start = 0
    while True:
        line = input_f.readline()
        if not line:
            break
        if line.startswith(START_PREFIX):
            start += 1
            if start >= 2:
                break
            continue
        print(line)
        key, value = line.strip().split('=')
        key, value = key.strip('"'), value.strip('"')
        try:
            value = int(value)
        except:
            pass
        metadata[key] = value
    print(metadata)

    # count the single data lines
    metadata_count = metadata.recordcount if input.endswith("load") else metadata.operationcount
    if args.kline is None:
        single_data_size = int(metadata_count / len(args.ips))
    else:
        single_data_size = int(args.kline * 1000 / len(args.ips))
    remaining = metadata_count - single_data_size * len(args.ips)
    print(f"{single_data_size = }")
    print(f"\033[;33mused = {single_data_size} * {len(args.ips)} = {single_data_size * len(args.ips)}, {remaining = }\033[0m")
    print(f"\033[;33mused = {single_data_size / MiB_TO_LINES:.2f} MiB * {len(args.ips)} = {single_data_size * len(args.ips) /MiB_TO_LINES:.2f} MiB, remaining = {remaining / MiB_TO_LINES:.2f} MiB (estimated)\033[0m")
    if remaining < 0:
        print("\033[;31m" + "Error: kline is too large or ycsb workload is too small. Tip: delete it to use all lines" + "\033[0m")
        exit(1)
    print()

    # create files
    files = []
    for i in args.ips:
        f = open(tmp_dir / f"{output}_{i}", 'a')
        files.append(f)
        f.write(f"{START_1}\n")
        for key, value in metadata.items():
            if key in ["recordcount", "operationcount"]:
                f.write(f'"{key}"="{single_data_size}"\n')
            else:
                f.write(f'"{key}"="{value}"\n')
        f.write(f"{START_2}\n")

    # read the remaining data, and write files
    line_num = 0
    while True:
        line = input_f.readline()
        files[line_num // single_data_size].write(line)
        if not line:
            break
        line_num += 1
        if line_num >= single_data_size * len(args.ips):
            break
    input_f.close()

    # flush (or) close files, otherwise the files will lose data
    for i in files:
        # i.flush()
        i.close()

    # send files
    for ip in args.ips:
        full_ip = args.ip_prefix + str(ip)
        print(f"\033[;33msend to {ip}\033[0m")
        user_name = os.popen('whoami').read().strip()
        project_name = os.popen('basename $(pwd)').read().strip()

        def print_and_os(cmd):
            print("\033[;32m" + cmd + "\033[0m")
            os.system(cmd)

        print_and_os(f"ssh-copy-id {user_name}@{full_ip}")
        print_and_os(f"ssh {user_name}@{full_ip} mkdir -p {args.project_parent}/{project_name}/{args.split_path}")
        print_and_os(f"scp {tmp_dir / f'{output}_{ip}'} {user_name}@{full_ip}:{args.project_parent}/{project_name}/{split_file}")
        print()

    # close files and delete files
    for f in files:
        try:
            os.remove(f.name)
        except:
            pass

tmp_dir.rmdir()
