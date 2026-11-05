import sys
import os
import subprocess
import tempfile
import shutil

def main():
    if len(sys.argv) != 3:
        sys.exit(1)
    
    tests_dir = sys.argv[1]
    main_dir = sys.argv[2]
    
    headers = [
        "af_ble_stats.h",
        "af_ble_snap.h",
        "af_ble_nvs.h"
    ]
    
    mut_tables = [
        ("ble_stats.mut", "af_ble_stats.h"),
        ("ble_snap.mut", "af_ble_snap.h"),
        ("ble_nvs.mut", "af_ble_nvs.h")
    ]
    
    tmp_root = tempfile.mkdtemp()
    
    def build(dirpath, out_bin):
        cmd = [
            "gcc", "-std=c11", "-O2", "-w",
            f"-I{dirpath}", f"-I{tests_dir}",
            "-o", out_bin,
            f"{tests_dir}/probe_e16.c",
            "-lm"
        ]
        try:
            res = subprocess.run(cmd, capture_output=True, timeout=120)
            return res.returncode == 0
        except Exception:
            return False

    def run(bin_path):
        try:
            res = subprocess.run(
                [bin_path],
                cwd=tests_dir,
                capture_output=True,
                timeout=120
            )
            return res.stdout.decode('ascii', errors='replace').splitlines()
        except subprocess.TimeoutExpired:
            return None
        except Exception:
            return None

    def copy_headers(dest_dir):
        os.makedirs(dest_dir, exist_ok=True)
        for h in headers:
            src = os.path.join(main_dir, h)
            dst = os.path.join(dest_dir, h)
            shutil.copy2(src, dst)

    # 1. Baseline
    base_dir = os.path.join(tmp_root, "base")
    copy_headers(base_dir)
    base_bin = os.path.join(tmp_root, "base_bin")
    
    if not build(base_dir, base_bin):
        print("BASELINE FAILED")
        shutil.rmtree(tmp_root, ignore_errors=True)
        sys.exit(2)
    
    base_lines = run(base_bin)
    if base_lines is None or len(base_lines) == 0:
        print("BASELINE FAILED")
        shutil.rmtree(tmp_root, ignore_errors=True)
        sys.exit(2)
        
    base_p = [l for l in base_lines if l.startswith("P ")]
    base_s = [l for l in base_lines if l.startswith("S ")]
    
    print(f"BASE P={len(base_p)} S={len(base_s)}")
    
    total_mutants = 0
    
    # 2. Mutants
    for table_name, target_header in mut_tables:
        table_path = os.path.join(tests_dir, table_name)
        if not os.path.exists(table_path):
            continue
            
        with open(table_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
            
        for line in lines:
            line = line.strip()
            if not line:
                continue
                
            parts = line.split('\t')
            if len(parts) != 3:
                continue
                
            label = parts[0]
            # parts[1] is expected red tests, ignored for logic
            sed_expr = parts[2]
            
            total_mutants += 1
            
            mut_dir = os.path.join(tmp_root, f"mut_{label}")
            copy_headers(mut_dir)
            
            target_file = os.path.join(mut_dir, target_header)
            
            # Read bytes before
            with open(target_file, 'rb') as f:
                before_bytes = f.read()
            
            # Apply sed
            try:
                subprocess.run(
                    ["sed", "-i", sed_expr, target_file],
                    capture_output=True,
                    timeout=10
                )
            except Exception:
                pass
            
            # Read bytes after
            with open(target_file, 'rb') as f:
                after_bytes = f.read()
                
            if before_bytes == after_bytes:
                print(f"{label}|SED_NOT_APPLIED")
                continue
                
            mut_bin = os.path.join(tmp_root, f"mut_bin_{label}")
            
            if not build(mut_dir, mut_bin):
                print(f"{label}|BUILD_FAILED")
                continue
                
            mut_lines = run(mut_bin)
            
            if mut_lines is None:
                print(f"{label}|TIMEOUT")
                continue
                
            mut_p = [l for l in mut_lines if l.startswith("P ")]
            mut_s = [l for l in mut_lines if l.startswith("S ")]
            
            # Calculate dP
            dP = 0
            min_len_p = min(len(base_p), len(mut_p))
            for i in range(min_len_p):
                if base_p[i] != mut_p[i]:
                    dP += 1
            dP += abs(len(base_p) - len(mut_p))
            
            # Calculate dS
            dS = 0
            min_len_s = min(len(base_s), len(mut_s))
            for i in range(min_len_s):
                if base_s[i] != mut_s[i]:
                    dS += 1
            dS += abs(len(base_s) - len(mut_s))
            
            print(f"{label}|P={dP}|S={dS}")
            
    print(f"TOTAL {total_mutants}")
    
    shutil.rmtree(tmp_root, ignore_errors=True)
    sys.exit(0)

if __name__ == "__main__":
    main()
