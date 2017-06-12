#!/bin/bash

KIND="erdos_400 erdos_800 barabasi_1 barabasi_2"
SALG="random highdegree"
PERCENTS="0.02 0.10 0.20 0.30 0.50 0.95"
declare -A NET
NET["fastnet"]="10000_2000"
NET["mediumnet"]="100000_20000"
NET["slownet"]="100000_20000"


gen_file() {
    cat << EOF
# Kind of network
network_kind = $K
network_size = 200
network_connectivity = $P # The meaning of this value depends on the kind of network
miners_percent = 0.5
selfish_percent = $PC
selfish_algo = $salg
selfish_power_percent = $PPC

# Delays in nanoseconds
base_delay = $BD
delay_per_transaction = $TD

# Block rewards
block_reward = 1
transaction_reward = 0.01

# Number of transactions included in a block by miners
transactions_per_block = 50

# Simulation parameters
seed = 1
nthreads = -1 # -1 means find out automatically
transaction_interval = 650 # in microseconds
block_interval = 10000 # in microseconds
block_num = 1000 
EOF
}

for i in $KIND
do
    K=$(echo $i | cut -d _ -f 1)
    P=$(echo $i | cut -d _ -f 2)
    for net in "${!NET[@]}"
    do
        BD=$(echo "${NET[$net]}" | cut -d _ -f 1)
        TD=$(echo "${NET[$net]}" | cut -d _ -f 2)
        echo $K $P $net $BD $TD
        PC=0.0
        PPC=0.0
        salg=random
        gen_file > ${K}_${P}_${net}_honest.cfg
        for PC in $PERCENTS
        do
            for PPC in $PERCENTS
            do
                for salg in $SALG
                do
                    gen_file > ${K}_${P}_${net}_${PC}_${PPC}_${salg}.cfg
                done
            done
        done
    done
done
