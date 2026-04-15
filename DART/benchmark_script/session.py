import time
import paramiko
from typing import List
from paramiko.channel import ChannelFile

from color import *



class Session:
    def __init__(self, host, username, password):
        self.host = host
        self.username = username
        self.password = password
        self.client = paramiko.SSHClient()
        self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.client.connect(self.host, username=self.username, password=self.password)
        self.channel = self.client.invoke_shell()


    def show_input(self, command):
        print(f"{GREEN}{BOLD}{self.username}@{self.host}{RESET}:{YELLOW}{BOLD}stdin{RESET}${YELLOW} {command}{RESET}")


    def quick_show_output(self, stdout: ChannelFile, stderr: ChannelFile, sleep_time: float):
        # no loop request (wait till .exit_status_ready()), async with ssh execute
        # so only for a quick output / error output just after .exec_command()
        time.sleep(sleep_time)
        if stdout.channel.recv_ready():
            quick_data = stdout.channel.recv(16384).decode("utf-8")
            print(f"{GREEN}{BOLD}{self.username}@{self.host}{RESET}:{BOLD}quick stdout{RESET}$ {quick_data}")
        if stderr.channel.recv_stderr_ready():
            quick_data = stderr.channel.recv_stderr(16384).decode("utf-8")
            print(f"{GREEN}{BOLD}{self.username}@{self.host}{RESET}:{RED}{BOLD}quick stderr{RESET}${RED} {quick_data}{RESET}")


    def execute(self, dir: str, project_name: str, command: str, quick_show_out=False, quick_show_sleep_time=0.2, sudo_S_num=0):
        self.show_input(command)
        stdin, stdout, stderr = self.client.exec_command(f"cd {dir}; cd {project_name}; " + command)

        if sudo_S_num > 0:
            stdin.write(''.join([self.password, '\n'] * sudo_S_num))

        if quick_show_out:
            self.quick_show_output(stdout, stderr, sleep_time=quick_show_sleep_time)

        return stdout.read().decode(), stderr.read().decode()


    def execute_non_blocking(self, dir: str, project_name: str, command: str, quick_show_out=False, quick_show_sleep_time=0.2, sudo_S_num=0):
        self.show_input(command)
        stdin, stdout, stderr = self.client.exec_command(f"cd {dir}; cd {project_name}; " + command)

        if sudo_S_num > 0:
            stdin.write(''.join([self.password, '\n'] * sudo_S_num))

        if quick_show_out:
            self.quick_show_output(stdout, stderr, sleep_time=quick_show_sleep_time)

        return stdout, stderr

    def execute_many(self, dir: str, project_name: str, commands: List[str], quick_show_out=False, quick_show_sleep_time=0.2, sudo_S_num=0):
        command = " && ".join(commands)
        self.show_input(command)
        stdin, stdout, stderr = self.client.exec_command(f"cd {dir}; cd {project_name}; " + command)

        if sudo_S_num > 0:
            stdin.write(''.join([self.password, '\n'] * sudo_S_num))

        if quick_show_out:
            self.quick_show_output(stdout, stderr, sleep_time=quick_show_sleep_time)

        return stdout.read().decode(), stderr.read().decode()


    def execute_many_non_blocking(self, dir: str, project_name: str, commands: List[str], quick_show_out=False, quick_show_sleep_time=0.2, sudo_S_num=0):
        command = " && ".join(commands)
        self.show_input(command)
        stdin, stdout, stderr = self.client.exec_command(f"cd {dir}; cd {project_name}; " + command)

        if sudo_S_num > 0:
            stdin.write(''.join([self.password, '\n'] * sudo_S_num))

        if quick_show_out:
            self.quick_show_output(stdout, stderr, sleep_time=quick_show_sleep_time)

        return stdout, stderr


    def close(self):
        try:
            self.client.close()
        except:
            pass

