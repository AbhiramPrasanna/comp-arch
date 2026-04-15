def build_sudo_exec_cmd(command: str, real_args: dict, range_index: int):
    return "sudo -S " + command + " " + " ".join(
        [(f"--{k}={v[range_index]}" if type(v) is list else f"--{k}={v}") for k, v in real_args.items()]
    )

def build_exec_cmd(command: str, real_args: dict, range_index: int):
    return command + " " + " ".join(
        [(f"--{k}={v[range_index]}" if type(v) is list else f"--{k}={v}") for k, v in real_args.items()]
    )
