#!/usr/bin/env python3

import os
from datetime import datetime
from pathlib import Path
import paramiko
import select
import time

from json_parser import parse
from args import build_exec_cmd, build_sudo_exec_cmd
from color import *
from session import Session



use_sudo = False
json_dir_name = "benchmark_json"
output_dir = "output"
project_name = Path.cwd().name
sleep_between_nodes = 5
sleep_between_benchmarks = 60



def generate_time_str():
    return datetime.now().strftime(r"%Y-%m-%d-%H%M%S-%f")

def receive_buffer_from_channelfile(channelfile_dict: dict[str, paramiko.ChannelFile]) -> dict[str, str]:
    """
    channelfile_dict: dict of `name (str): stdout (channelfile)`
    return: dict of `name (str): buffer (str)`
    """

    # channelfile_list: list of all `channelfile (stdout)`
    channelfile_list = [channelfile for channelfile in channelfile_dict.values()]

    # channel_buffer_map: dict of `hash_stdout_channel: {machine_name, buffer}`
    channel_buffer_map = {
        hash(channelfile.channel): {"name": name, "buffer": ""}
        for name, channelfile in channelfile_dict.items()
    }

    # use select to listen to all the client socket file descriptors
    while len(channelfile_list) > 0:

        time.sleep(1)  # mitigate the busy loop

        channel_list = [channelfile.channel for channelfile in channelfile_list]

        if any([channel.recv_ready() for channel in channel_list]):
            read_list, _, _ = select.select(channel_list, [], [], 0.0)
            for c in read_list:
                length = 16384
                print(f"--- will receive up to {length} bytes from stdout, if it's too short or too long, please change it manually")
                recved = c.recv(length).decode("utf-8")
                channel_buffer_map[hash(c)]["buffer"] += recved
                print(f"{channel_buffer_map[hash(c)]['name']} output (stdout & stderr): \n{recved}")

        # remove file discriptors that have closed.
        new_channelfile_list = []
        for channelfile in channelfile_list:
            if channelfile.channel.exit_status_ready():
                print(channel_buffer_map[hash(channelfile.channel)]["name"], 'receive done')
                continue
            new_channelfile_list.append(channelfile)
        channelfile_list = new_channelfile_list

    return {x["name"]: x["buffer"] for x in channel_buffer_map.values()}


Path(output_dir).mkdir(exist_ok=True)
benchmark_list = parse(json_dir_name)

for benchmark_num, benchmark in enumerate(benchmark_list):

    # all range multiple together
    range_index_tuple_list = [[]]
    for nodes_num, nodes in enumerate(benchmark):
        new_range_index_tuple_list = []
        for each_tuple in range_index_tuple_list:
            for range_index in range(nodes["\\len"]):
                new_range_index_tuple_list.append(each_tuple + [range_index])
        range_index_tuple_list = new_range_index_tuple_list

    for range_index_tuple_num, range_index_tuple in enumerate(range_index_tuple_list):
        now_time = generate_time_str()
        print(f"{YELLOW}run benchmark {benchmark_num}: {range_index_tuple_num + 1}/{len(range_index_tuple_list)}, nodes num = {len(benchmark)}, select arg = {range_index_tuple}{RESET}")

        channelfile_dict = {}
        session_list = []
        for node_num, node in enumerate(benchmark):
            machine_name, command_name = node["machine_name"], node["command_name"]
            ip, user, passwd, directory = node["ip"], node["user"], node["password"], node["project_parent_path"]
            if use_sudo:
                exec_cmd = build_sudo_exec_cmd(node["\\command"], node["\\real_args"], range_index_tuple[node_num])
            else:
                exec_cmd = build_exec_cmd(node["\\command"], node["\\real_args"], range_index_tuple[node_num])
            print(f"{PURPLE}{BOLD}{machine_name}-{command_name} ${RESET}{CYAN} {exec_cmd}{RESET}")
            session = Session(ip, user, passwd)
            session_list.append(session)
            res_out, _ = session.execute_non_blocking(directory, project_name, exec_cmd, sudo_S_num=(1 if use_sudo else 0))
            res_out.channel.set_combine_stderr(True)
            channelfile_dict[f"{machine_name}-{command_name}-{range_index_tuple}"] = res_out
            if node_num != len(benchmark) - 1:
                print(f"{YELLOW}sleep {sleep_between_nodes} seconds{RESET}")
                time.sleep(sleep_between_nodes)

        buffer_dict = receive_buffer_from_channelfile(channelfile_dict)

        for name, buffer in buffer_dict.items():
            Path(output_dir).mkdir(exist_ok=True)
            with open(f"{output_dir}/{name}-{now_time}.txt", "w") as f:
                f.write(buffer)

        for node_num, node in enumerate(benchmark):
            command = node["\\command"]
            directory = node["project_parent_path"]
            session_list[node_num].execute(directory, project_name, f"sudo -S killall {command} || true", sudo_S_num=1)

        for session in session_list:
            session.close()

        if range_index_tuple_num != len(range_index_tuple_list) - 1:
            print(f"{YELLOW}sleep {sleep_between_benchmarks} seconds{RESET}")
            time.sleep(sleep_between_benchmarks)
