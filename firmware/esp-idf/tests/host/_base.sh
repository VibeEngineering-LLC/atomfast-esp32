cd /mnt/c/Temp/atomfast_fw/tests/host
make test 2>&1 | tail -12
make clean
bash build_pfs_sim.sh 300 /tmp/pfs_base && timeout 300 /tmp/pfs_base crash 40 1 | tail -2
