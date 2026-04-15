#!/usr/bin/env python3

import re
import os
import json
import shutil
import urllib3
import tarfile
from tqdm import tqdm
from pathlib import Path


download_dir = Path("./workload/")
search_url = 'https://api.github.com/repos/brianfrankcooper/YCSB/releases/latest'
search_pattern = re.compile("ycsb-.*?.tar.gz")
strip_end = ".tar.gz"


def get_latest_release_data():
    print('Checking for latest YCSB release on GitHub...')
    http = urllib3.PoolManager()
    req = http.request('GET', search_url, headers={'user-agent': 'node.js'})
    res = req.data.decode('utf-8')
    try:
        data = json.loads(res)
    except json.JSONDecodeError as e:
        print(e.msg, e)
        print(repr(res))
        exit(0)
    html_url = data['html_url'].replace('/tag/', '/download/').rstrip('/')
    for asset in data.get('assets', ()):
        name = asset['name']
        grp = search_pattern.match(name)
        if grp.groups() is not None:
            return name, html_url + '/' + name, asset['size']
    raise SystemExit('Failed to find the installer package on github') 


def download(url: str, size: int, target: str):
    print(f'Downloading {url}...')
    http = urllib3.PoolManager()
    with http.request('GET', url, preload_content=False) as r:
        with tqdm.wrapattr(r, "read", total=size, desc="") as raw:
            with open(target, 'wb') as out_file:       
                shutil.copyfileobj(raw, out_file)


def extract(source: Path, dest: Path):
    print(f'Extracting {source}...')
    with tarfile.open(source) as tar:
        for member in tqdm(iterable=tar.getmembers(), total=len(tar.getmembers())):
            tar.extract(member, dest)


name, url, size = get_latest_release_data()
print(f'The latest version is {name}')

download_dir.mkdir(parents=True, exist_ok=True)
filename = name.strip(strip_end)

if os.path.exists(download_dir / filename):
    print(f'{filename} already extracted')
else:
    if os.path.exists(download_dir / name):
        print(f'{name} already downloaded')
    else:
        download(url, size, download_dir / name)
    extract(download_dir / name, download_dir)
