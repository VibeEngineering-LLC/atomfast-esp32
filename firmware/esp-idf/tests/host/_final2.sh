cd /mnt/c/Temp/atomfast_fw/tests/host
echo "### make test"; make clean >/dev/null; make test 2>&1 | grep -E "TESTS|PASSED|FAIL"; make clean >/dev/null
echo "### asan persist"; make asan 2>&1 | grep -E "TESTS|FAIL" 
echo "### inject (fixed)"; bash run_inject.sh
echo "### store"; bash run_store_sim.sh
echo "### crash"; for s in 1 2 3; do /tmp/pfs_inj crash 100 $s 2>&1 | grep CRASH; done
echo "### mut_pfs (G1-G8)"; bash mut_pfs.sh 60
