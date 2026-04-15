#!/usr/bin/env python3

import os
import re
import json
from pathlib import Path

from color import *

# read from all .json and put them to ane dict (if is a list, use "\\list\\file_name" as key)
def read_json_files_from_dir(dir_path):
    res = {}
    dir_path = Path(dir_path)
    for res_file_name in os.listdir(dir_path):
        # if end with .json
        if Path(res_file_name).suffix != ".json":
            continue
        with open(dir_path / res_file_name, "r") as res_file:
            res_list_or_dict = json.load(res_file)
            if isinstance(res_list_or_dict, list):
                res[f"\\list\\{res_file_name}"] = res_list_or_dict
            else:
                for key, value in res_list_or_dict.items():
                    res[f"{key}\\{res_file_name}"] = value
    return res


def select_unique_data(data_dict):
    list_pattern = re.compile(r"\\list\\(.+)")

    field_list = ["machine", "command", "benchmark"]
    pattern_list = [
        re.compile(r"machine\\(.+)"),
        re.compile(r"command\\(.+)"),
        re.compile(r"benchmark\\(.+)")
    ]
    used_list = [False, False, False]

    result = {}

    for key, value in data_dict.items():

        find = False

        if matched := list_pattern.match(key):
            print(f"{YELLOW}ignore {matched.group(1)}: it's a list{RESET}")
            continue

        for i in range(len(field_list)):
            if matched := pattern_list[i].match(key):
                if not used_list[i]:
                    used_list[i] = True
                    print(f"{GREEN}{field_list[i]} field: use data in {matched.group(1)}{RESET}")
                    result[field_list[i]] = value
                else:
                    print(f"{YELLOW}ignore field {field_list[i]} in {matched.group(1)}: {field_list[i]} already used{RESET}")
                find = True
            if find:
                break

        if not find:
            print(f"{RED}ignore {key}: not match any field{RESET}")
    
    return result


def expand_benchmark(data_dict):
    # search which machine
    machine: dict = data_dict["machine"]
    command: dict = data_dict["command"]
    benchmark: list = data_dict["benchmark"]

    same_pattern = re.compile(r"\\same(?:\\(.+))?")
    data_pattern = re.compile(r"\\data(?:\\(.+))")

    for single_benchmark in benchmark:

        # expand depend data pattern (0)
        depend_expand_save = {}
        depend_detect = {}

        # range for all nodes
        range_save = []

        for nodes in single_benchmark:

            # expand depend data pattern (0)
            # depend detect pre save
            depend_detect.setdefault(nodes["command_name"], [])
            depend_detect[nodes["command_name"]].append(nodes["machine_name"])

            # expand depend data pattern (1)
            raw_depend = nodes.setdefault("\\depend", {})

            for depend_value in raw_depend.values():
                if depend_value in depend_expand_save:
                    continue
                elif type(depend_value) is not str:
                    continue
                elif match := data_pattern.match(depend_value):
                    data = match.group(1).split("\\")
                    if data[1] == '': data[1] = 0
                    else: data[1] = int(data[1])

                    # just save, expand later
                    depend_expand_save[depend_value] = data

            # expand range pattern (including same)
            raw_range = nodes.setdefault("\\range", {})

            range_dict = {}
            range_len = 1

            for new_range_key_or_matchsame, new_value_list_or_dict in raw_range.items():
                # old data
                new_range_dict = {}
                for range_key in range_dict.keys():
                    new_range_dict[range_key] = []
                old_range_len = 1
                for range_value in range_dict.values():
                    old_range_len = len(range_value)
                    break

                # new data * old data
                new_range_len = 1
                if match := same_pattern.match(new_range_key_or_matchsame):
                    # count new data length
                    for new_range_key, new_range_value in new_value_list_or_dict.items():
                        new_range_len = len(new_range_value)
                        new_range_dict[new_range_key] = []
                    # input new data
                    for i in range(old_range_len):
                        for j in range(new_range_len):
                            for old_key in range_dict.keys():
                                new_range_dict[old_key].append(range_dict[old_key][i])
                            for new_key in new_value_list_or_dict.keys():
                                new_range_dict[new_key].append(new_value_list_or_dict[new_key][j])
                else:
                    # count new data length
                    new_range_len = len(new_value_list_or_dict)
                    new_range_dict[new_range_key_or_matchsame] = []
                    # input new data
                    for i in range(old_range_len):
                        for j in range(new_range_len):
                            for old_key in range_dict.keys():
                                new_range_dict[old_key].append(range_dict[old_key][i])
                            new_range_dict[new_range_key_or_matchsame].append(new_value_list_or_dict[j])

                range_dict = new_range_dict
                range_len = new_range_len * old_range_len

            del nodes["\\range"]
            nodes["\\range"] = range_dict
            nodes["\\range"]["\\len"] = range_len

            range_save.append(range_dict)

        # expand the depend data pattern (2)
        for depend_key, depend_value in depend_expand_save.items():
            command_name, data_index, machine_key, *remain_data = depend_value
            machine_name = depend_detect[command_name][data_index]
            real_value = machine[machine_name][machine_key]
            depend_expand_save[depend_key] = real_value
        for nodes in single_benchmark:
            raw_depend = nodes.get("\\depend", {})
            for depend_key, depend_value in raw_depend.items():
                if depend_value in depend_expand_save:
                    nodes["\\depend"][depend_key] = depend_expand_save[depend_value] + ''.join(remain_data)

        # all length
        total_range_len = 1
        for i in range_save:
            lens = i["\\len"]
            print(f'{YELLOW}range len: {lens}{RESET}')
            total_range_len *= lens
        print(f"{YELLOW}total range len: {total_range_len}{RESET}")

        # change the real args
        for nodes in single_benchmark:
            # raw_command_command = command[nodes["command_name"]]["run_command"]
            raw_command_args = command[nodes["command_name"]]["run_args"]
            nodes.update(machine[nodes["machine_name"]])
            nodes["\\command"] = command[nodes["command_name"]]["run_command"]
            nodes["\\real_args"] = {}
            nodes["\\len"] = nodes["\\range"]["\\len"]
            for key, value in raw_command_args.items():
                if value == "\\overwrite":
                    # range first
                    if key in nodes["\\range"]:
                        nodes["\\real_args"][key] = nodes["\\range"][key]
                    elif key in nodes["\\depend"]:
                        nodes["\\real_args"][key] = nodes["\\depend"][key]
                    else:
                        print(f"{RED}error: {key} \\overwrite, but not found in \\depend or \\range{RESET}")
                else:
                    nodes["\\real_args"][key] = value
            del nodes["\\range"]
            del nodes["\\depend"]

    return data_dict["benchmark"]


def parse(dir_path):
    res = read_json_files_from_dir(dir_path)
    res = select_unique_data(res)
    res = expand_benchmark(res)
    return res


if __name__ == "__main__":
    res = parse("benchmark_json")
    write_to = "tmp.result.json"
    with open(write_to, "w") as res_file:
        json.dump(res, res_file, indent=4)
    print(f"{GREEN}write to {write_to}{RESET}")
