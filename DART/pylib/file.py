import yaml
from pathlib import Path
from datetime import datetime


def load_config(config_file_path: str) -> dict:
    with open(config_file_path, "r") as config_file:
        config_yaml = yaml.load(config_file, yaml.Loader)
        return config_yaml


def generate_time_str():
    return datetime.now().strftime(r"%Y-%m-%d-%H-%M-%S-%f")


def generate_bench_result_filename(time_str: str):
    Path("./result/json/").mkdir(parents=True, exist_ok=True)
    return f"./result/json/{time_str}.json"


def generate_single_testcase_picture_filename(type: str, time_str: str):
    Path("./result/img/").mkdir(parents=True, exist_ok=True)
    return f"./result/img/{type}_{time_str}.png"


def generate_compare_testcases_picture_filename(
    about: str, fix_value: str, time_str: str
):
    Path("./result/compare/").mkdir(parents=True, exist_ok=True)
    return f"./result/compare/{about}_WITH_{fix_value}_TIME_{time_str}.png"
