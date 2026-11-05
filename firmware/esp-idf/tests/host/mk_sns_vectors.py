import sys
import struct

def crc8(data, poly=0x31, init=0xFF):
    crc = init
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc

def bme280_calc(t1, t2, t3, p1, p2, p3, p4, p5, p6, p7, p8, p9, h1, h2, h3, h4, h5, h6, adc_p, adc_t, adc_h):
    # Temperature
    var1 = (adc_t / 16384.0 - t1 / 1024.0) * t2
    var2 = ((adc_t / 131072.0 - t1 / 8192.0) * (adc_t / 131072.0 - t1 / 8192.0)) * t3
    t_fine = int(var1 + var2)
    t_c = (var1 + var2) / 5120.0

    # Pressure (Bosch BME280 DS002, Appendix A, double)
    var1 = t_fine / 2.0 - 64000.0
    var2 = var1 * var1 * p6 / 32768.0
    var2 = var2 + var1 * p5 * 2.0
    var2 = (var2 / 4.0) + (p4 * 65536.0)
    var1 = (p3 * var1 * var1 / 524288.0 + p2 * var1) / 524288.0
    var1 = (1.0 + var1 / 32768.0) * p1
    p_pa = 1048576.0 - adc_p
    p_pa = (p_pa - (var2 / 4096.0)) * 6250.0 / var1
    var1 = p9 * p_pa * p_pa / 2147483648.0
    var2 = p_pa * p8 / 32768.0
    p_pa = p_pa + (var1 + var2 + p7) / 16.0

    # Humidity (Appendix A, double)
    var_h = t_fine - 76800.0
    var_h = (adc_h - (h4 * 64.0 + h5 / 16384.0 * var_h)) * (h2 / 65536.0 * (1.0 + h6 / 67108864.0 * var_h * (1.0 + h3 / 67108864.0 * var_h)))
    var_h = var_h * (1.0 - h1 * var_h / 524288.0)
    rh = min(100.0, max(0.0, var_h))


    return t_c, p_pa, rh

def main():
    if len(sys.argv) < 2:
        sys.exit(1)
    out_path = sys.argv[1]

    # Calibration values
    T1 = 27504
    T2 = 26435
    T3 = -1000
    P1 = 36477
    P2 = -10685
    P3 = 3024
    P4 = 2855
    P5 = 140
    P6 = -7
    P7 = 15500
    P8 = -14600
    P9 = 6000
    H1 = 75
    H2 = 361
    H3 = 0
    H4 = 317
    H5 = 50
    H6 = 30

    # Build SV_BME_CAL_A (26 bytes: 0x88..0xA1)
    # 0x88,0x89: T1 u16 LE
    # 0x8A,0x8B: T2 s16 LE
    # 0x8C,0x8D: T3 s16 LE
    # 0x8E,0x8F: P1 u16 LE
    # 0x90,0x91: P2 s16 LE
    # 0x92,0x93: P3 s16 LE
    # 0x94,0x95: P4 s16 LE
    # 0x96,0x97: P5 s16 LE
    # 0x98,0x99: P6 s16 LE
    # 0x9A,0x9B: P7 s16 LE
    # 0x9C,0x9D: P8 s16 LE
    # 0x9E,0x9F: P9 s16 LE
    # 0xA0: 0
    # 0xA1: H1

    def u16_le(v):
        return [v & 0xFF, (v >> 8) & 0xFF]

    def s16_le(v):
        v = v & 0xFFFF
        return [v & 0xFF, (v >> 8) & 0xFF]

    cal_a = []
    cal_a += u16_le(T1)
    cal_a += s16_le(T2)
    cal_a += s16_le(T3)
    cal_a += u16_le(P1)
    cal_a += s16_le(P2)
    cal_a += s16_le(P3)
    cal_a += s16_le(P4)
    cal_a += s16_le(P5)
    cal_a += s16_le(P6)
    cal_a += s16_le(P7)
    cal_a += s16_le(P8)
    cal_a += s16_le(P9)
    cal_a.append(0)
    cal_a.append(H1 & 0xFF)

    # Build SV_BME_CAL_B (7 bytes: 0xE1..0xE7)
    # E1,E2: H2 s16 LE
    # E3: H3 u8
    # E4: (H4 >> 4) & 0xFF
    # E5: (H4 & 0x0F) | ((H5 & 0x0F) << 4)
    # E6: (H5 >> 4) & 0xFF
    # E7: H6 & 0xFF

    cal_b = []
    cal_b += s16_le(H2)
    cal_b.append(H3 & 0xFF)
    cal_b.append((H4 >> 4) & 0xFF)
    cal_b.append((H4 & 0x0F) | ((H5 & 0x0F) << 4))
    cal_b.append((H5 >> 4) & 0xFF)
    cal_b.append(H6 & 0xFF)

    # BME280 vectors
    bme_vectors = [
        (415148, 519888, 24000),
        (415148, 519888, 30000),
        (415148, 519888, 40000),
        (400000, 500000, 20000),
        (400000, 500000, 35000),
        (450000, 540000, 28000),
        (450000, 540000, 45000),
        (380000, 480000, 33000),
        (415148, 560000, 26000),
        (415148, 470000, 38000),
        (420000, 520000, 10000),
        (420000, 520000, 50000),
    ]

    bme_entries = []
    for adc_p, adc_t, adc_h in bme_vectors:
        t_c, p_pa, rh = bme280_calc(T1, T2, T3, P1, P2, P3, P4, P5, P6, P7, P8, P9, H1, H2, H3, H4, H5, H6, adc_p, adc_t, adc_h)
        # Pack raw bytes
        p_msb = (adc_p >> 12) & 0xFF
        p_lsb = (adc_p >> 4) & 0xFF
        p_xlsb = (adc_p & 0x0F) << 4
        t_msb = (adc_t >> 12) & 0xFF
        t_lsb = (adc_t >> 4) & 0xFF
        t_xlsb = (adc_t & 0x0F) << 4
        h_msb = (adc_h >> 8) & 0xFF
        h_lsb = adc_h & 0xFF
        raw = [p_msb, p_lsb, p_xlsb, t_msb, t_lsb, t_xlsb, h_msb, h_lsb]
        bme_entries.append((raw, t_c, p_pa, rh))

    # SHT3x vectors
    sht_pairs = [
        (0x6666, 0x8000),
        (0x0000, 0x0000),
        (0xFFFF, 0xFFFF),
        (0x6A3D, 0x7C28),
        (0x5000, 0x9999),
        (0x7FFF, 0x4000),
    ]

    sht_entries = []
    for s_t, s_rh in sht_pairs:
        t_c = -45.0 + 175.0 * s_t / 65535.0
        rh = 100.0 * s_rh / 65535.0
        t_msb = (s_t >> 8) & 0xFF
        t_lsb = s_t & 0xFF
        t_crc = crc8([t_msb, t_lsb])
        rh_msb = (s_rh >> 8) & 0xFF
        rh_lsb = s_rh & 0xFF
        rh_crc = crc8([rh_msb, rh_lsb])
        raw = [t_msb, t_lsb, t_crc, rh_msb, rh_lsb, rh_crc]
        sht_entries.append((raw, t_c, rh))

    # Check CRC8 for 0xBEEF
    assert crc8([0xBE, 0xEF]) == 0x92, "CRC8 check failed for 0xBEEF"

    # AHT2x vectors
    aht_pairs = [
        (0x80000, 0x66666),
        (0x00000, 0x00000),
        (0xFFFFF, 0xFFFFF),
        (0x7C28F, 0x5999A),
        (0x4CCCC, 0x70000),
    ]

    aht_entries = []
    for s_rh, s_t in aht_pairs:
        rh = s_rh / (2.0 ** 20) * 100.0
        t_c = s_t / (2.0 ** 20) * 200.0 - 50.0
        raw = [0x1C]
        raw.append((s_rh >> 12) & 0xFF)
        raw.append((s_rh >> 4) & 0xFF)
        raw.append(((s_rh & 0x0F) << 4) | (s_t >> 16))
        raw.append((s_t >> 8) & 0xFF)
        raw.append(s_t & 0xFF)
        crc_val = crc8(raw)
        raw.append(crc_val)
        aht_entries.append((raw, t_c, rh))

    # Generate C header
    lines = []
    lines.append("#ifndef SNS_VECTORS_H")
    lines.append("#define SNS_VECTORS_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("/* BME280 Calibration */")
    lines.append("static const uint8_t SV_BME_CAL_A[26] = {")
    lines.append("    " + ", ".join("0x%02X" % b for b in cal_a))
    lines.append("};")
    lines.append("")
    lines.append("static const uint8_t SV_BME_CAL_B[7] = {")
    lines.append("    " + ", ".join("0x%02X" % b for b in cal_b))
    lines.append("};")
    lines.append("")
    lines.append("#define SV_BME_T1 %d" % T1)
    lines.append("#define SV_BME_T2 %d" % T2)
    lines.append("#define SV_BME_T3 %d" % T3)
    lines.append("#define SV_BME_P1 %d" % P1)
    lines.append("#define SV_BME_P2 %d" % P2)
    lines.append("#define SV_BME_P3 %d" % P3)
    lines.append("#define SV_BME_P4 %d" % P4)
    lines.append("#define SV_BME_P5 %d" % P5)
    lines.append("#define SV_BME_P6 %d" % P6)
    lines.append("#define SV_BME_P7 %d" % P7)
    lines.append("#define SV_BME_P8 %d" % P8)
    lines.append("#define SV_BME_P9 %d" % P9)
    lines.append("#define SV_BME_H1 %d" % H1)
    lines.append("#define SV_BME_H2 %d" % H2)
    lines.append("#define SV_BME_H3 %d" % H3)
    lines.append("#define SV_BME_H4 %d" % H4)
    lines.append("#define SV_BME_H5 %d" % H5)
    lines.append("#define SV_BME_H6 %d" % H6)
    lines.append("")
    lines.append("/* BME280 Vectors */")
    lines.append("typedef struct {")
    lines.append("    uint8_t raw[8];")
    lines.append("    double t_c;")
    lines.append("    double p_pa;")
    lines.append("    double rh;")
    lines.append("} sv_bme_t;")
    lines.append("")
    lines.append("static const sv_bme_t SV_BME[12] = {")
    for i, (raw, t_c, p_pa, rh) in enumerate(bme_entries):
        raw_str = ", ".join("0x%02X" % b for b in raw)
        lines.append("    { { %s }, %.6f, %.6f, %.6f }%s" % (raw_str, t_c, p_pa, rh, "," if i < 11 else ""))
    lines.append("};")
    lines.append("")
    lines.append("/* SHT3x Vectors */")
    lines.append("typedef struct {")
    lines.append("    uint8_t raw[6];")
    lines.append("    double t_c;")
    lines.append("    double rh;")
    lines.append("} sv_sht_t;")
    lines.append("")
    lines.append("static const sv_sht_t SV_SHT[6] = {")
    for i, (raw, t_c, rh) in enumerate(sht_entries):
        raw_str = ", ".join("0x%02X" % b for b in raw)
        lines.append("    { { %s }, %.6f, %.6f }%s" % (raw_str, t_c, rh, "," if i < 5 else ""))
    lines.append("};")
    lines.append("")
    lines.append("#define SV_CRC8_BEEF 0x92")
    lines.append("")
    lines.append("/* AHT2x Vectors */")
    lines.append("typedef struct {")
    lines.append("    uint8_t raw[7];")
    lines.append("    double t_c;")
    lines.append("    double rh;")
    lines.append("} sv_aht_t;")
    lines.append("")
    lines.append("static const sv_aht_t SV_AHT[5] = {")
    for i, (raw, t_c, rh) in enumerate(aht_entries):
        raw_str = ", ".join("0x%02X" % b for b in raw)
        lines.append("    { { %s }, %.6f, %.6f }%s" % (raw_str, t_c, rh, "," if i < 4 else ""))
    lines.append("};")
    lines.append("")
    lines.append("#endif /* SNS_VECTORS_H */")

    content = "\n".join(lines) + "\n"
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(content)

if __name__ == "__main__":
    main()
