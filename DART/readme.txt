tee and sed
    # bash, not fish
    your_command | tee >(sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' > output.log)

output name recommand:
    output-(HART)-1M-1C-20M-[a].json
    output-(HART)-3M-3C-60M-[a,b,c,load,ma,mload].json
    output-(LoFr-Hash-NoSk)-3M-3C-60M-[a,b,c,load,ma,mload]-2.json

which name:
    Lock?-Hash?-Skip?:
        (bad)   (good)
        Lock or LoFr
        ReAl or Hash
        NoSk or Skip
    HART (all good)
    naive (all bad)

which file:
    a,b,c,d,load,ma,mb,mc,md,mload

appendix:
    nothing
    -hand
    -normal
    -1
    -2

auto-appendix:
    -may-error (if all var > 1)
    -contain-some-error (if some var > 1)
