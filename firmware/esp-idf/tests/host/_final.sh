cd /mnt/c/Temp/atomfast_fw/tests/host
echo "### mut_e8"; bash mut_e8.sh
echo "### persist.mut"; MT_TARGET=test-persist bash mutate.sh persist.mut ../../main/af_persist.h 2>&1 | grep -E "^==|VERDICT" | paste - - | cut -c1-200
