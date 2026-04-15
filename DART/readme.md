# DART

[SIGMOD'26]: DART: A Lock-free Two-layer Hashed ART Index for Disaggregated Memory

## Compile

### Clone

```shell
git clone <this repo>
git submodule update --init --recursive
```

### Build


```shell
sudo apt install libboost-context-dev libboost-coroutine-dev
cmake -B build
cmake --build build
```



## Workload Generation


```shell
./script/workload_download.py
./script/workload_gen.sh
```

```shell
./script/split_and_send_workload.py --inputs a_load a_run --outputs a_load_split a_run_split --ips 70 72 74
```

## Run

```shell
sudo sysctl -w vm.nr_hugepages=16384
```

```shell
bin/monitor --test_func=0 --memory_num=1 --compute_num=1 --load_thread_num=56 --run_thread_num=56 --coro_num=1 --mem_mb=8192 --th_mb=10 --workload_load=c_load --workload_run=c_run --bucket=256 --run_max_request=6000000
bin/memory --monitor_addr=127.0.0.1:9898  
bin/compute --monitor_addr=127.0.0.1:9898  
```

## Note

The RACE hashing part of this code is based on the implementation from:
https://github.com/minxinhao/SepHash

You are welcome to substitute this component with a more efficient hash table if one is available.



