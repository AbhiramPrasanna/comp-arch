# Benchmark Script

## Usage

All `*.json` files in this directory will be treated as the benchmark input. See `bench.json.bak` for detail imformation (and copy it to bench.json for your own usage).

## Keyword

|name|usage|
|---|---|
|"\\"|all keyword begin (remember to double it in json)|
|"\\\\"|real "\\" (TODO)|
|"\\overwrite"|will be overwroten by "\\depend" or "\\range"|
|"\\depend"|use in command field: this argument will be specified to a new value (usually by "\\data") in benchmark field|
|"\\range"|use in command field: this argument will be specified in some range in benchmark field|
|"\\same", "\\same[\\{tags}]"|use in benchmark "\\range": two (or more) args change simultaneously|
|"\\data\\{command_name}\\{num(or-empty-str-means-0)}\\{key}[\\{data-to-concat}]"|use in benchmark "\\depend": change to the value of such key|

## Run

