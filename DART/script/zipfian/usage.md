# zipf with cpp

## build

```bash
cd script/zipfian
python3 -m pip install pybind11
python3 setup.py build_ext --inplace
cd ../..
```

## usage

```bash
# in prheart/
# zipf_para: 0~1
script/zipfian/gen_email.py --input /opt/email_filtered.txt --max 100000 --zipf_para 0.99
```