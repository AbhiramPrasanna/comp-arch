#!/usr/bin/env python3

import re
import os
import sys
import json
from pathlib import Path

# read all files from a dir
def read_files_from_dir(dir_path):
    walk_gen = os.walk(dir_path)
    _, dir_list, file_list = next(walk_gen)
    dir_list.sort()
    file_list.sort()
    return dir_list, file_list

dir_path = sys.argv[1]
dir_list, file_list = read_files_from_dir(dir_path)

match_regs = {
    "old-thread": re.compile(r"thread_num = ([0-9]*)"),  # TODO: remove this old code
    "thread": re.compile(r"run_thread_num = ([0-9]*)"),
    "rtt_band": re.compile(r"avg rtt count: ([0-9\.]*); avg bandwidth consumption: ([0-9\.]*);"),
    "thpt": re.compile(r"Total throughput = ([0-9\.]*) MOps"),
    "lat": re.compile(r"Average latency = ([0-9\.]*) us"),
    "thpt_var": re.compile(r"thp = ([0-9\.]*) MOps"),
}

names = {
    "old-thread": "old-thread",  # TODO: remove this old code
    "thread": "thread",
    "rtt_band": ("rtt", "band"),
    "thpt": "thpt",
    "lat": "lat",
    "thpt_var": "thpt_var",
}

# if not float
types = {
    "old-thread": int,  # TODO: remove this old code
    "thread": int,
}

data = {}
data["name"] = Path(dir_path).stem

for file_name in file_list:
    filepath = Path(dir_path) / file_name
    unique_id = file_name.split("-")[2]
    with open(filepath, "r") as file:
        data.setdefault(unique_id, {})
        for line in file:
            for name, reg in match_regs.items():
                matched = reg.search(line)
                if matched is not None and len(matched.groups()) > 0:
                    if len(matched.groups()) > 1:
                        for index, i in enumerate(matched.groups()):
                            data[unique_id].setdefault(names[name][index], [])
                            data[unique_id][names[name][index]].append(i)
                    else:
                        data[unique_id].setdefault(names[name], [])
                        data[unique_id][names[name]].append(matched.group(1))

max_var = 0
min_var = 9999999

for unique_id, file_save in data.items():
    if type(file_save) is not dict:
        continue
    for name, value_list in file_save.items():
        if name == "thpt_var":
            ave = sum([types.get(name, float)(i) for i in value_list]) / len(value_list)
            for i in range(len(value_list)):
                value_list[i] = (types.get(name, float)(value_list[i]) / ave)
            ave = 1
            var = sum([(types.get(name, float)(i) - ave) ** 2 for i in value_list]) / len(value_list)
            var *= 100
            file_save[name] = var
            max_var = max(max_var, var)
            min_var = min(min_var, var)
            continue
        if len(value_list) > 1:
            ave = sum([types.get(name, float)(i) for i in value_list]) / len(value_list)
            file_save[name] = ave
        else:
            file_save[name] = types.get(name, float)(value_list[0])
            ave = file_save[name]

    # TODO: remove this old code (begin)
    if "thread" in file_save and "old-thread" in file_save:
        del file_save["old-thread"]
    elif "old-thread" in file_save:
        file_save["thread"] = file_save["old-thread"]
        del file_save["old-thread"]
    # TODO: remove this old code (end)

    print(unique_id)
    print(file_save)

json_name_stem = Path(dir_path).stem
if min_var > 1:
    json_name_stem += "-may-error"
elif max_var > 1:
    json_name_stem += "-contain-some-error"

#save to file, name same as dir.json
with open(f"{json_name_stem}.json", "w") as file:
    json.dump(data, file, indent=4)
