# AtomFast Gateway ESP-IDF preview

This directory archives the exact project and managed-component inputs of the tested `0.1.0-e16i` diagnostic candidate, published as `v0.1.0-e16i-preview.1`.

Target: ESP32-S3. Toolchain: ESP-IDF 5.4.0. Configuration: `sdkconfig.candidate`. Partition table: `partitions.csv`. The SDK Docker image digest and SHA256 of every archived build input are listed in `release-provenance.json`. Managed dependencies and their licenses are included so their exact source content is preserved. Host/web test sources are supplemental and are not compiler inputs.

The release asset `atomfast_gw.bin` is the existing OTA application image tested on the hardware, not a complete flash image. Its SHA256 is `86b963bf7247e38225a547d0d14f76ee9bc93036e49ee3fd66ca30c1415efc53`. Its embedded firmware version remains `0.1.0-e16i`; the preview tag is a publication label. The tag archives its build inputs after testing, so a new build may differ in Git/version/time metadata and must be validated separately. This snapshot does not claim that rebuilding the new tag produces a byte-identical binary.

One-hour API load: 11,704 successful GET requests, 180 primary samples, no recorded HTTP/transport/parse errors, no reboot or observed BLE errors, changing BLE averages, minimum internal heap 66,747 bytes. Median API latency by window: 67–76 ms. Aggregate receiver row/cursor progress agreed across three databases over a wider observation interval; payload-by-payload integrity was not established.

Known limitations: 18 successful responses took at least one second (maximum 1.581 s); an earlier separate warmup had a 3.009 s timeout of unknown cause. The historical browser connection indicator, original root-page latency gate and full payload integrity remain open. BMP280 was disconnected, so physical I2C recovery was not tested. Hosted CI has not been validated for this new source layout. This preview is not the stable/latest release.

Archive scope: 427 of the original 429 snapshot entries are committed byte-for-byte. One additional entry, sdkconfig.defaults, has only an internal audit label removed from a comment; all non-comment configuration lines are unchanged and both hashes are recorded. The remaining entry is a nested `.git` pointer (repository metadata), explicitly recorded by path and hash under `excluded_build_snapshot_metadata`; its content is not published. This excludes no firmware source file. Supplemental tests are listed separately.
