# батарея стенда pfs_sim: usage pfs_bat.sh <метка>
cd /project/tests/host
bash build_pfs_sim.sh 300 /tmp/pfs_b 2>&1 | tail -1
for s in 1 2; do timeout -s KILL 280 /tmp/pfs_b crash 300 $s 2>&1 | grep CRASH; done
bash run_inject.sh 2>&1
